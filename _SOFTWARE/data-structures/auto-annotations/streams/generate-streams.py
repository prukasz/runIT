"""Generate the board → app stream catalog from the system data connectors.

Every frame the board sends starts with its connector's header byte (the
"stream"): text logs, binary errors, telemetry, command responses. The system
connectors are declared once, in sys_data_connector_init()
(components/system/sys_data_connector/sys_data_connector.c):

  {.id = SYS_DATA_CONNECTOR_LOGS, .name = "logs", .header = CONFIG_TX_PACKET_CLASS_LOGS, .system = true},

The generator reads that table, resolves the header byte from sdkconfig and the
connector ID, alias and description from the published sys_data_connector_id_e
enum (//#ref-enum), so the app finds every stream by name instead of by a
hard-coded byte.

It also publishes where each stream travels over BLE: the runIT service and
characteristic UUIDs (//@STATIC_SERVICE / //@STATIC_CHARACTERISTIC defines in
components/runit/runit_board_defs.h) and the board's bindings
(sys_data_connector_bind_tx / _rx with the BLE provider in
runit_board_connector_bindings_init(), runit_board_cfg.c).

And what a board holds after a restart, so the app compares its settings
editors against it instead of hard-coded copies:
  - ble.characteristics: every sys_ble_char_cfg_t runit_board_ble_init()
    creates (flags, name, TX / RX buffer bytes, SYS_BUFF_SIZE_FOR evaluated);
  - bindings: every sys_data_connector_bind_tx / _rx of every provider in
    runit_board_connector_bindings_init(), `#if CONFIG_...` blocks resolved
    against sdkconfig (the condition is kept in `when`);
  - limits: the connector registry Kconfig values (CONFIG_SYS_DATA_CONNECTOR_*);
  - streams[].max_frame: the frame cap of each system connector.

Usage:
  python data-structures/auto-annotations/streams/generate-streams.py
"""
import importlib.util
import json
import re
import sys
from pathlib import Path

try:
    import jsonschema
except ImportError:
    jsonschema = None

PROJECT_ROOT = Path(__file__).parents[3]
SOURCE_PATH = PROJECT_ROOT / "components" / "system" / "sys_data_connector" / "sys_data_connector.c"
SDKCONFIG_PATH = PROJECT_ROOT / "sdkconfig"
BOARD_DEFS_PATH = PROJECT_ROOT / "components" / "runit" / "runit_board_defs.h"
BOARD_CFG_PATH = PROJECT_ROOT / "components" / "runit" / "runit_board_cfg.c"
OUT_PATH = PROJECT_ROOT / "data-structures" / "streams" / "streams.generated.json"
SCHEMA_PATH = PROJECT_ROOT / "data-structures" / "schema" / "streams.schema.json"
SCHEMA_REL_PATH = SCHEMA_PATH.relative_to(PROJECT_ROOT).as_posix()

ENUMS_GENERATOR_PATH = PROJECT_ROOT / "data-structures" / "auto-annotations" / "enums" / "generate-enums.py"
spec = importlib.util.spec_from_file_location("generate_enums", ENUMS_GENERATOR_PATH)
generate_enums = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = generate_enums
spec.loader.exec_module(generate_enums)

TABLE_RE = re.compile(r"s_system_connectors\[\]\s*=\s*\{(?P<body>.*?)\n\s*\};", re.DOTALL)
ENTRY_RE = re.compile(r'\{\s*\.id\s*=\s*(?P<id>\w+)\s*,\s*\.name\s*=\s*"(?P<name>\w+)"\s*,\s*\.header\s*=\s*(?P<header>CONFIG_\w+)')
STATIC_UUID_RE = re.compile(r"^\s*#define\s+(?P<name>\w+)\s+(?P<value>0x[0-9A-Fa-f]{4})\s*//@STATIC_(?P<kind>SERVICE|CHARACTERISTIC)", re.MULTILINE)
BINDINGS_RE = re.compile(r"runit_board_connector_bindings_init\s*\(\s*void\s*\)\s*\{(?P<body>.*?)\n\}", re.DOTALL)
BLE_PROVIDER_RE = re.compile(r"const\s+uint8_t\s+(?P<var>\w+)\s*=\s*RUNIT_DATA_PROVIDER_BLE\s*;")
BIND_RE = r"sys_data_connector_bind_(?P<dir>tx|rx)\(\s*(?P<connector>SYS_DATA_CONNECTOR_\w+)\s*,\s*{var}\s*,\s*(?P<uuid>\w+)\s*\)"
SDKCONFIG_RE = re.compile(r"^(CONFIG_\w+)=(\S+)\s*$", re.MULTILINE)
COMPONENTS_DIR = PROJECT_ROOT / "components"
BLE_INIT_RE = re.compile(r"runit_board_ble_init\s*\(\s*void\s*\)\s*\{(?P<body>.*?)\n\}", re.DOTALL)
SVC_CFG_RE = re.compile(r"sys_ble_svc_cfg_t\s+\w+\s*=\s*\{(?P<fields>[^}]*)\}\s*;")
CHAR_CFG_RE = re.compile(r"sys_ble_char_cfg_t\s+(?P<var>\w+)\s*=\s*\{(?P<fields>[^}]*)\}\s*;")
CHAR_CREATE_RE = re.compile(r"sys_ble_char_create\(\s*(?P<svc>\w+)\s*,\s*&(?P<var>\w+)\s*\)")
PROVIDER_VAR_RE = re.compile(r"const\s+uint8_t\s+(?P<var>\w+)\s*=\s*(?P<provider>RUNIT_DATA_PROVIDER_\w+)\s*;")
ANY_BIND_RE = re.compile(r"sys_data_connector_bind_(?P<dir>tx|rx)\(\s*(?P<connector>SYS_DATA_CONNECTOR_\w+)\s*,\s*(?P<var>\w+)\s*,\s*(?P<endpoint>\w+)\s*\)")
DEFINE_RE = re.compile(r"^\s*#define\s+(?P<name>\w+)\s+(?P<value>0x[0-9A-Fa-f]+|\d+)u?\b", re.MULTILINE)
BUFF_SIZE_RE = re.compile(r"^SYS_BUFF_SIZE_FOR\(\s*(?P<item>\w+)\s*,\s*(?P<count>\d+)\s*\)$")
LIMITS = {
    "connectors_max": "CONFIG_SYS_DATA_CONNECTOR_MAX",
    "providers_per_connector_max": "CONFIG_SYS_DATA_CONNECTOR_PROVIDERS_MAX",
    "name_max": "CONFIG_SYS_DATA_CONNECTOR_NAME_MAX",
    "frame_max": "CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX",
}


def fail(message: str) -> None:
    raise SystemExit(f"ERROR: {message}")


def ble_layout() -> tuple[dict, dict[str, dict]]:
    """(transport entry, per-connector {notify, write} characteristic UUIDs) of the BLE provider."""
    uuids = {m.group("name"): (m.group("kind"), f"0x{int(m.group('value'), 16):04X}") for m in STATIC_UUID_RE.finditer(BOARD_DEFS_PATH.read_text(encoding="utf-8", errors="replace"))}
    services = [value for kind, value in uuids.values() if kind == "SERVICE"]
    if len(services) != 1:
        fail(f"{BOARD_DEFS_PATH.name}: expected one //@STATIC_SERVICE, found {len(services)}")
    text = BOARD_CFG_PATH.read_text(encoding="utf-8", errors="replace")
    body = BINDINGS_RE.search(text)
    provider = BLE_PROVIDER_RE.search(body.group("body")) if body else None
    if not body or not provider:
        fail(f"{BOARD_CFG_PATH.name}: runit_board_connector_bindings_init() with a RUNIT_DATA_PROVIDER_BLE variable not found")
    bindings: dict[str, dict] = {}
    for bind in re.finditer(BIND_RE.format(var=re.escape(provider.group("var"))), body.group("body")):
        kind_value = uuids.get(bind.group("uuid"))
        if not kind_value or kind_value[0] != "CHARACTERISTIC":
            fail(f"{BOARD_CFG_PATH.name}: {bind.group('uuid')} is not a //@STATIC_CHARACTERISTIC in {BOARD_DEFS_PATH.name}")
        key = "notify" if bind.group("dir") == "tx" else "write"
        entry = bindings.setdefault(bind.group("connector"), {})
        if key in entry:
            fail(f"{bind.group('connector')} is bound to two BLE {key} characteristics")
        entry[key] = kind_value[1]
    if not bindings:
        fail(f"{BOARD_CFG_PATH.name}: no BLE bindings in runit_board_connector_bindings_init()")
    transport = {"service": services[0], "source_files": [BOARD_DEFS_PATH.relative_to(PROJECT_ROOT).as_posix(), BOARD_CFG_PATH.relative_to(PROJECT_ROOT).as_posix()]}
    return transport, bindings


def read_config() -> dict[str, str]:
    return dict(SDKCONFIG_RE.findall(SDKCONFIG_PATH.read_text(encoding="utf-8", errors="ignore"))) if SDKCONFIG_PATH.exists() else {}


def header_defines() -> dict[str, int]:
    """Every integer #define in the components' headers (endpoint and buffer constants)."""
    defines: dict[str, int] = {}
    for header in COMPONENTS_DIR.rglob("*.h"):
        for match in DEFINE_RE.finditer(header.read_text(encoding="utf-8", errors="replace")):
            defines.setdefault(match.group("name"), int(match.group("value"), 0))
    return defines


def split_fields(text: str) -> dict[str, str]:
    """`.a = 1, .b = f(x, y), .c = "s"` → {a: '1', b: 'f(x, y)', c: '"s"'} (commas inside () or "" kept)."""
    parts, depth, quoted, current = [], 0, False, ""
    for char in text:
        if char == '"':
            quoted = not quoted
        elif not quoted and char == "(":
            depth += 1
        elif not quoted and char == ")":
            depth -= 1
        if char == "," and depth == 0 and not quoted:
            parts.append(current)
            current = ""
        else:
            current += char
    parts.append(current)
    fields = {}
    for part in (entry.strip() for entry in parts):
        if not part:
            continue
        match = re.match(r"^\.(\w+)\s*=\s*(.+)$", part, re.DOTALL)
        if not match:
            fail(f"{BOARD_CFG_PATH.name}: can't read initializer field '{part}'")
        fields[match.group(1)] = " ".join(match.group(2).split())
    return fields


def evaluate(expression: str, config: dict[str, str], defines: dict[str, int], where: str) -> int:
    """An integer initializer: a literal, a #define, a CONFIG_ value or SYS_BUFF_SIZE_FOR(item, count)."""
    expression = expression.strip()
    if re.fullmatch(r"0x[0-9A-Fa-f]+u?|\d+u?", expression):
        return int(expression.rstrip("u"), 0)
    if expression in ("true", "false"):
        return int(expression == "true")
    if expression.startswith("CONFIG_"):
        if expression not in config:
            fail(f"{where}: {expression} not found in sdkconfig")
        return int(config[expression], 0)
    if expression in defines:
        return defines[expression]
    buffer = BUFF_SIZE_RE.match(expression)
    if buffer:
        # sys_buffers.h: (align4(item_max) + SYS_BUFF_ITEM_HDR_LEN) * max(count, 2)
        item = evaluate(buffer.group("item"), config, defines, where)
        count = int(buffer.group("count"))
        return (((item + 3) & ~3) + defines["SYS_BUFF_ITEM_HDR_LEN"]) * max(count, 2)
    fail(f"{where}: can't evaluate '{expression}' (literal, #define, CONFIG_ or SYS_BUFF_SIZE_FOR only)")


def board_gatt(uuids: dict[str, tuple[str, str]], config: dict[str, str], defines: dict[str, int]) -> tuple[bool, list[dict]]:
    """(service is_primary, characteristics) as runit_board_ble_init() creates them."""
    text = BOARD_CFG_PATH.read_text(encoding="utf-8", errors="replace")
    body = BLE_INIT_RE.search(text)
    if not body:
        fail(f"{BOARD_CFG_PATH.name}: runit_board_ble_init() not found")
    service = SVC_CFG_RE.search(body.group("body"))
    if not service:
        fail(f"{BOARD_CFG_PATH.name}: runit_board_ble_init() creates no sys_ble_svc_cfg_t")
    is_primary = bool(evaluate(split_fields(service.group("fields")).get("is_primary", "false"), config, defines, "service"))
    created = {match.group("var"): match.group("svc") for match in CHAR_CREATE_RE.finditer(body.group("body"))}
    characteristics = []
    for match in CHAR_CFG_RE.finditer(body.group("body")):
        var = match.group("var")
        if var not in created:
            continue
        fields = split_fields(match.group("fields"))
        symbol = fields.get("uuid", "")
        kind_value = uuids.get(symbol)
        if not kind_value or kind_value[0] != "CHARACTERISTIC":
            fail(f"{BOARD_CFG_PATH.name}: {var}.uuid '{symbol}' is not a //@STATIC_CHARACTERISTIC in {BOARD_DEFS_PATH.name}")
        desc = fields.get("desc", '""')
        if not (desc.startswith('"') and desc.endswith('"')):
            fail(f"{BOARD_CFG_PATH.name}: {var}.desc must be a string literal")
        flag = lambda key: bool(evaluate(fields.get(key, "false"), config, defines, f"{var}.{key}"))
        characteristics.append({
            "symbol": symbol,
            "uuid": kind_value[1],
            "name": desc[1:-1],
            "write": flag("is_write"),
            "notify": flag("is_notify"),
            "indicate": flag("is_indicate"),
            "tx_buffer_size": evaluate(fields.get("tx_buffer_size", "0"), config, defines, f"{var}.tx_buffer_size"),
            "rx_buffer_size": evaluate(fields.get("rx_buffer_size", "0"), config, defines, f"{var}.rx_buffer_size"),
        })
    if not characteristics:
        fail(f"{BOARD_CFG_PATH.name}: runit_board_ble_init() creates no characteristics")
    return is_primary, characteristics


def active_lines(body: str, config: dict[str, str]) -> list[tuple[str, str | None]]:
    """(line, condition) of a function body, `#if CONFIG_X` / `#ifdef` blocks resolved against sdkconfig."""
    lines, stack = [], []
    for line in body.splitlines():
        directive = re.match(r"^\s*#\s*(if|ifdef|ifndef|else|endif)\b\s*(\w*)", line)
        if directive:
            kind, name = directive.groups()
            if kind in ("if", "ifdef"):
                stack.append((name, config.get(name, "n") not in ("n", "0", "")))
            elif kind == "ifndef":
                stack.append((f"!{name}", config.get(name, "n") in ("n", "0", "")))
            elif kind == "else" and stack:
                name, enabled = stack.pop()
                stack.append((f"!{name}" if not name.startswith("!") else name[1:], not enabled))
            elif kind == "endif" and stack:
                stack.pop()
            continue
        if all(enabled for _, enabled in stack):
            lines.append((line, stack[-1][0] if stack else None))
    return lines


def board_bindings(uuids: dict[str, tuple[str, str]], config: dict[str, str], defines: dict[str, int], symbols: dict) -> list[dict]:
    """Every binding runit_board_connector_bindings_init() makes with this sdkconfig, any provider."""
    text = BOARD_CFG_PATH.read_text(encoding="utf-8", errors="replace")
    body = BINDINGS_RE.search(text)
    if not body:
        fail(f"{BOARD_CFG_PATH.name}: runit_board_connector_bindings_init() not found")
    providers: dict[str, str] = {}
    bindings = []
    for line, condition in active_lines(body.group("body"), config):
        provider = PROVIDER_VAR_RE.search(line)
        if provider:
            providers[provider.group("var")] = provider.group("provider")
        for bind in ANY_BIND_RE.finditer(line):
            provider_symbol = providers.get(bind.group("var"))
            if not provider_symbol:
                fail(f"{BOARD_CFG_PATH.name}: binding through '{bind.group('var')}', which is no RUNIT_DATA_PROVIDER_ variable")
            endpoint_symbol = bind.group("endpoint")
            if endpoint_symbol in uuids:
                endpoint = int(uuids[endpoint_symbol][1], 16)
            elif endpoint_symbol in defines:
                endpoint = defines[endpoint_symbol]
            else:
                fail(f"{BOARD_CFG_PATH.name}: endpoint {endpoint_symbol} is no //@STATIC_ UUID or integer #define")
            connector = symbols.get(bind.group("connector"))
            provider_value = symbols.get(provider_symbol)
            if not connector or not provider_value:
                fail(f"{bind.group('connector')} / {provider_symbol} not in a published (//#ref-enum) enum")
            bindings.append({
                "connector": bind.group("connector"),
                "connector_id": connector["value"],
                "direction": bind.group("dir"),
                "provider": provider_symbol,
                "provider_id": provider_value["value"],
                "endpoint": endpoint,
                "endpoint_symbol": endpoint_symbol,
                **({"when": condition} if condition else {}),
            })
    return bindings


def build_document() -> dict:
    text = SOURCE_PATH.read_text(encoding="utf-8", errors="replace")
    table = TABLE_RE.search(text)
    if not table:
        fail(f"{SOURCE_PATH.name}: s_system_connectors[] table not found")
    entries = list(ENTRY_RE.finditer(table.group("body")))
    if not entries:
        fail(f"{SOURCE_PATH.name}: s_system_connectors[] has no {{.id, .name, .header}} entries")

    config = read_config()
    symbols = generate_enums.scan(generate_enums.COMPONENTS_DIR)["symbols"]
    ble, ble_bindings = ble_layout()
    defines = header_defines()
    uuids = {m.group("name"): (m.group("kind"), f"0x{int(m.group('value'), 16):04X}") for m in STATIC_UUID_RE.finditer(BOARD_DEFS_PATH.read_text(encoding="utf-8", errors="replace"))}
    limits = {}
    for key, name in LIMITS.items():
        if name not in config:
            fail(f"{name} not found in sdkconfig")
        limits[key] = int(config[name], 0)
    is_primary, characteristics = board_gatt(uuids, config, defines)
    ble = {"service": ble["service"], "service_symbol": next(name for name, (kind, _) in uuids.items() if kind == "SERVICE"), "is_primary": is_primary, "characteristics": characteristics, "source_files": ble["source_files"]}
    bindings = board_bindings(uuids, config, defines, symbols)
    frame_caps = {m.group("id"): m.group("max_frame") for m in re.finditer(r"\.id\s*=\s*(?P<id>\w+)[^}]*?\.max_frame\s*=\s*(?P<max_frame>\w+)", table.group("body"))}
    streams = []
    for entry in entries:
        symbol = symbols.get(entry.group("id"))
        if not symbol:
            fail(f"{entry.group('id')} is not a member of a published (//#ref-enum) enum")
        raw = config.get(entry.group("header"))
        if raw is None:
            fail(f"{entry.group('header')} not found in sdkconfig")
        streams.append({
            "name": entry.group("name"),
            "header": f"0x{int(raw, 0):02X}",
            "config": entry.group("header"),
            "connector": entry.group("id"),
            "connector_enum": symbol["enum"],
            "connector_id": symbol["value"],
            "alias": symbol.get("alias", entry.group("name")),
            "description": symbol.get("description", ""),
            # 0 / unset = CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX (sys_data_connector_create)
            "max_frame": evaluate(frame_caps[entry.group("id")], config, defines, entry.group("id")) or limits["frame_max"] if entry.group("id") in frame_caps else limits["frame_max"],
            **({"ble": ble_bindings[entry.group("id")]} if entry.group("id") in ble_bindings else {}),
        })
    for key in ("name", "header", "connector_id"):
        values = [stream[key] for stream in streams]
        if len(values) != len(set(values)):
            fail(f"two system connectors share a {key}: {values}")
    return {"$schema": SCHEMA_REL_PATH, "schemaVersion": 1, "kind": "streams", "source_file": SOURCE_PATH.relative_to(PROJECT_ROOT).as_posix(), "limits": limits, "ble": ble, "streams": streams, "bindings": bindings}


def validate(document: dict) -> None:
    if jsonschema is None:
        print("WARNING: jsonschema package not installed - skipping schema validation")
        return
    schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
    errors = sorted(jsonschema.Draft202012Validator(schema).iter_errors(document), key=lambda error: list(error.path))
    if errors:
        details = "\n".join(f"  - at {'/'.join(map(str, error.path)) or '<root>'}: {error.message}" for error in errors)
        fail(f"streams failed schema validation:\n{details}")


def main() -> int:
    document = build_document()
    validate(document)
    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUT_PATH.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    listed = ", ".join(f"{stream['name']} {stream['header']}" for stream in document["streams"])
    print(f"Wrote {OUT_PATH.relative_to(PROJECT_ROOT)} ({listed})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
