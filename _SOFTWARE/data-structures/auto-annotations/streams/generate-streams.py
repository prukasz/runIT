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


def build_document() -> dict:
    text = SOURCE_PATH.read_text(encoding="utf-8", errors="replace")
    table = TABLE_RE.search(text)
    if not table:
        fail(f"{SOURCE_PATH.name}: s_system_connectors[] table not found")
    entries = list(ENTRY_RE.finditer(table.group("body")))
    if not entries:
        fail(f"{SOURCE_PATH.name}: s_system_connectors[] has no {{.id, .name, .header}} entries")

    config = dict(SDKCONFIG_RE.findall(SDKCONFIG_PATH.read_text(encoding="utf-8", errors="ignore"))) if SDKCONFIG_PATH.exists() else {}
    symbols = generate_enums.scan(generate_enums.COMPONENTS_DIR)["symbols"]
    ble, ble_bindings = ble_layout()
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
            **({"ble": ble_bindings[entry.group("id")]} if entry.group("id") in ble_bindings else {}),
        })
    for key in ("name", "header", "connector_id"):
        values = [stream[key] for stream in streams]
        if len(values) != len(set(values)):
            fail(f"two system connectors share a {key}: {values}")
    return {"$schema": SCHEMA_REL_PATH, "schemaVersion": 1, "kind": "streams", "source_file": SOURCE_PATH.relative_to(PROJECT_ROOT).as_posix(), "ble": ble, "streams": streams}


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
