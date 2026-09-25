"""Generate the error catalog (tags, owners, severities, payload layouts, messages) from the C error maps.

Source of truth: components/sys_errors/codes/sys_error_codes.h, which includes
every module's errors/sys_error_<module>.h and combines their X-macro maps into
SYS_ERROR_MAP, SYS_OWNER_MAP and SYS_ERROR_LOGGER_MAP (layout: SYS_ERRORS.MD,
"Error maps and aggregation"). The maps are read in that aggregation order,
because the firmware's schema ID (SE_schema_id(), sys_error.c) hashes the tags
in map order; the generator computes the same ID so the app can tell whether
its catalog matches the firmware that sent an error packet.

Payloads keep native C layout on the wire (SYS_ERRORS.MD, "Logging and
telemetry"), so every payload field gets its offset with the ESP32-S3
alignment rules (natural alignment, little endian).

Messages come from each tag's LOG_BODY_<tag> macro: the format string and
arguments of its snprintf(). An argument passed through a C function (for
example esp_err_to_name) is published as `via`. When that function is a
plain value → text table in the error headers (a switch of `case N: return
"text";` or a `(r) == N ? "text" : ...` chain), the table is published in
`value_names`, so the app prints the same text as the firmware. A body the
generator can't reduce to one snprintf over payload fields gets no message,
and the app falls back to listing the fields.

Payload fields that hold an ID the app can name carry an annotation right
after the field's ';' inside the map (a block comment, because the maps are
one-line macros):

  struct { uint8_t dev_id; /*@id device*/ uint8_t mode; /*@enum-ref sys_io_mode_e*/ }

`@enum-ref <enum>` names a published (//#ref-enum) enum; `@id <kind>` one of
ID_KINDS, whose names live in another generated file or in this one. The
PARENT_KINDS also name the payload field their value depends on:
`@id rx-packet <class field>`, `@id contract-feature <contract field>`.

This file also publishes names the error headers only reach at run time:
esp_err_t names from the ESP-IDF the firmware is built with
(esp_err_to_name.c), and per-contract feature names from the tables marked
`//@contract-features $SYS_DEVICE_CONTRACT_<X>` (sys_io.c, sys_power.c,
sys_hbridge.c; feature ID = index, as SYS_DEV_FEATURE_ID computes it).

Usage:
  python data-structures/auto-annotations/errors/generate-errors.py
"""
import importlib.util
import json
import os
import re
import sys
from pathlib import Path

try:
    import jsonschema
except ImportError:
    jsonschema = None

PROJECT_ROOT = Path(__file__).parents[3]
COMPONENTS_DIR = PROJECT_ROOT / "components"
CODES_PATH = COMPONENTS_DIR / "sys_errors" / "codes" / "sys_error_codes.h"
BASE_PATH = COMPONENTS_DIR / "sys_errors" / "codes" / "sys_error_base.h"
SDKCONFIG_PATH = PROJECT_ROOT / "sdkconfig"
OUT_PATH = PROJECT_ROOT / "data-structures" / "errors" / "errors.generated.json"
SCHEMA_PATH = PROJECT_ROOT / "data-structures" / "schema" / "errors.schema.json"
SCHEMA_REL_PATH = SCHEMA_PATH.relative_to(PROJECT_ROOT).as_posix()

# ESP32-S3 (Xtensa LX7): natural alignment, 8-byte types aligned to 8.
C_TYPES = {
    "uint8_t": 1, "int8_t": 1, "bool": 1, "char": 1,
    "uint16_t": 2, "int16_t": 2,
    "uint32_t": 4, "int32_t": 4, "float": 4, "esp_err_t": 4,
    "uint64_t": 8, "int64_t": 8, "double": 8,
}
# Published wire type of each C type (esp_err_t is int32_t).
WIRE_TYPES = {"esp_err_t": "int32_t", "bool": "uint8_t", "char": "uint8_t"}

FNV_OFFSET = 2166136261
FNV_PRIME = 16777619

INCLUDE_RE = re.compile(r'^\s*#include\s+"(?P<name>[\w.]+)"', re.MULTILINE)
MAP_MEMBER_RE = re.compile(r"\b(\w+)\s*\(\s*X\s*\)")
DEFINE_RE = re.compile(r"^\s*#define\s+(?P<name>\w+)\s*\((?P<params>[^)]*)\)(?P<body>.*)$", re.MULTILINE)
LEVEL_ENUM_RE = re.compile(r"typedef\s+enum\s+se_level_e\s*\{(?P<body>.*?)\}\s*se_level_e\s*;", re.DOTALL)
LEVEL_MEMBER_RE = re.compile(r"(SE_LEVEL_\w+)\s*=\s*(\d+)")
FIELD_RE = re.compile(r"^(?P<type>\w+)\s+(?P<name>\w+)\s*(?:\[\s*(?P<len>\d+)\s*\])?$")
CAST_RE = re.compile(r"^\(\s*(?:unsigned\s+long|unsigned\s+int|unsigned|long|int|double|float|u?int\d+_t)\s*\)\s*")
FIELD_REF_RE = re.compile(r"^\(p\)->(\w+)$")
VIA_REF_RE = re.compile(r"^(\w+)\(\s*\(p\)->(\w+)\s*\)$")
CONVERSION_RE = re.compile(r"%(?:%|[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l|z|j|t)?[diouxXeEfgGcsp])")
NAME_FUNCTION_RE = re.compile(r"const\s+char\s*\*\s*(?P<name>\w+)\s*\(\s*\w+\s+\w+\s*\)\s*\{")
C_STRING = r'"((?:\\.|[^"\\])*)"'
CASE_RE = re.compile(r"case\s+(\d+)\s*:\s*return\s+" + C_STRING + r"\s*;")
DEFAULT_RE = re.compile(r"default\s*:\s*return\s+" + C_STRING + r"\s*;")
TERNARY_CASE_RE = re.compile(r"\(\s*\w+\s*\)\s*==\s*(\d+)\s*\?\s*" + C_STRING)
TERNARY_DEFAULT_RE = re.compile(r":\s*" + C_STRING + r"\s*\)\s*$")
SDKCONFIG_RE = re.compile(r"^(CONFIG_\w+)=(\S+)\s*$", re.MULTILINE)
ANNOTATION_RE = re.compile(r"/\*@(.*?)\*/", re.DOTALL)
ESP_ERR_ROW_RE = re.compile(r"ERR_TBL_IT\((?P<name>\w+)\),\s*/\*\s*(?P<value>-?\d+)(?:\s+0x[0-9a-fA-F]+)?\s+(?P<description>.*?)\s*\*/")

# What an `@id <kind>` payload field holds, and where the app finds its name:
ID_KINDS = {
    "device": "board device ID (board.generated.json devices[].id)",
    "error-tag": "error tag ID (this file, tags[].id)",
    "error-owner": "error owner ID (this file, owners[].id)",
    "error-level": "severity (this file, levels[].value)",
    "rx-class": "command class byte (contracts / settings class_header)",
    "rx-packet": "command packet byte within the class held by the parent field (contracts / settings packet_header)",
    "contract-feature": "feature ID (contract member index) within the device contract held by the parent field (this file, contract_features)",
    "vm-block-type": "VM block type (vm/blocks/index.generated.json blocks[].id)",
    "esp-err": "esp_err_t code (this file, esp_errors)",
}
# Kinds whose value only means something together with another payload field (`@id <kind> <parent field>`).
PARENT_KINDS = {"rx-packet", "contract-feature"}

# `//@contract-features $SYS_DEVICE_CONTRACT_<X>` above `const char* const <name>[] = {"a", "b", NULL};`
CONTRACT_FEATURES_RE = re.compile(r"//@contract-features\s+\$(?P<symbol>\w+)[^\n]*\n\s*const\s+char\s*\*\s*const\s+(?P<table>\w+)\s*\[\s*\]\s*=\s*\{(?P<body>.*?)\}\s*;", re.DOTALL)
CONTRACT_ENUM = "sys_device_contract_type_e"

ENUMS_GENERATOR_PATH = PROJECT_ROOT / "data-structures" / "auto-annotations" / "enums" / "generate-enums.py"
_spec = importlib.util.spec_from_file_location("generate_enums", ENUMS_GENERATOR_PATH)
generate_enums = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = generate_enums
_spec.loader.exec_module(generate_enums)
PUBLISHED_ENUMS = set(generate_enums.scan(generate_enums.COMPONENTS_DIR)["enums"])

PROJECT_DESCRIPTION_PATH = PROJECT_ROOT / "build" / "project_description.json"
ESP_ERR_TABLE = Path("components") / "esp_common" / "src" / "esp_err_to_name.c"


def fail(message: str) -> None:
    raise SystemExit(f"ERROR: {message}")


def logical_text(path: Path) -> str:
    """File text with line continuations joined and comments removed, except /*@...*/ field annotations."""
    text = path.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")
    text = re.sub(r"\\[ \t]*\n", " ", text)
    text = re.sub(r"/\*(?!@).*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"(?<!:)//[^\n]*", "", text)


def split_top_level(text: str, sep: str = ",") -> list[str]:
    """Split on separators outside (), {}, [] and string literals."""
    parts, depth, current, quote, escape = [], 0, [], None, False
    for char in text:
        if quote:
            current.append(char)
            if escape:
                escape = False
            elif char == "\\":
                escape = True
            elif char == quote:
                quote = None
            continue
        if char in "\"'":
            quote = char
        elif char in "([{":
            depth += 1
        elif char in ")]}":
            depth -= 1
        elif char == sep and depth == 0:
            parts.append("".join(current).strip())
            current = []
            continue
        current.append(char)
    tail = "".join(current).strip()
    if tail:
        parts.append(tail)
    return parts


def call_args(text: str, start: int) -> tuple[str, int]:
    """Argument text of a call whose '(' is at text[start]; returns (inside, index after ')')."""
    depth, quote, escape = 0, None, False
    for index in range(start, len(text)):
        char = text[index]
        if quote:
            if escape:
                escape = False
            elif char == "\\":
                escape = True
            elif char == quote:
                quote = None
            continue
        if char in "\"'":
            quote = char
        elif char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                return text[start + 1:index], index + 1
    fail(f"unbalanced parentheses in: {text[start:start + 80]}")


def x_entries(body: str) -> list[list[str]]:
    """Arguments of every X(...) in a map body."""
    entries = []
    for match in re.finditer(r"\bX\s*\(", body):
        inside, _ = call_args(body, match.end() - 1)
        entries.append(split_top_level(inside))
    return entries


def find_headers() -> dict[str, Path]:
    headers = {}
    for path in sorted(COMPONENTS_DIR.rglob("*.h")):
        if path.parent.name in ("errors", "codes"):
            if path.name in headers:
                fail(f"two error map headers named {path.name}: {headers[path.name]} and {path}")
            headers[path.name] = path
    return headers


def aggregate_members(codes_text: str, macro: str) -> list[str]:
    match = re.search(rf"#define\s+{macro}\s*\(\s*X\s*\)(?P<body>[^\n]*)", codes_text)
    if not match:
        fail(f"{CODES_PATH.name}: no #define {macro}(X)")
    members = MAP_MEMBER_RE.findall(match.group("body"))
    if not members:
        fail(f"{CODES_PATH.name}: {macro} lists no maps")
    return members


def c_unescape(literal: str) -> str:
    out, index = [], 0
    escapes = {"n": "\n", "t": "\t", "r": "\r", "\\": "\\", '"': '"', "'": "'", "0": "\0"}
    while index < len(literal):
        char = literal[index]
        if char == "\\" and index + 1 < len(literal):
            out.append(escapes.get(literal[index + 1], literal[index + 1]))
            index += 2
            continue
        out.append(char)
        index += 1
    return "".join(out)


def parse_payload(struct_text: str, where: str) -> dict:
    match = re.fullmatch(r"struct\s*\{(?P<body>.*)\}", struct_text.strip(), re.DOTALL)
    if not match:
        fail(f"{where}: payload is not an inline 'struct {{ ... }}': {struct_text[:80]}")
    fields, offset, align = [], 0, 1
    for chunk in match.group("body").split(";"):
        # A /*@...*/ annotation follows its field's ';', so it opens the next chunk.
        for annotation in ANNOTATION_RE.findall(chunk):
            if not fields:
                fail(f"{where}: annotation '/*@{annotation}*/' has no field before it")
            apply_annotation(fields[-1], annotation.strip(), where)
        declaration = ANNOTATION_RE.sub(" ", chunk).strip()
        if not declaration:
            continue
        field = FIELD_RE.match(re.sub(r"\s+", " ", declaration))
        if not field:
            fail(f"{where}: can't parse payload field '{declaration}'")
        c_type, name, length = field.group("type"), field.group("name"), field.group("len")
        if c_type not in C_TYPES:
            fail(f"{where}: payload field '{name}' has unsupported type '{c_type}' (add it to C_TYPES)")
        size = C_TYPES[c_type]
        offset = (offset + size - 1) // size * size
        align = max(align, size)
        entry = {"name": name, "type": WIRE_TYPES.get(c_type, c_type), "offset": offset}
        if c_type in WIRE_TYPES:
            entry["c_type"] = c_type
        if length:
            entry["array_len"] = int(length)
        fields.append(entry)
        offset += size * (int(length) if length else 1)
    if not fields:
        fail(f"{where}: empty payload struct")
    names = {field["name"] for field in fields}
    for field in fields:
        parent_field = field.get("id", {}).get("parent_field")
        if parent_field is not None and parent_field not in names:
            fail(f"{where}: {field['name']} names parent field '{parent_field}', which the payload doesn't have")
    return {"size": (offset + align - 1) // align * align, "fields": fields}


def apply_annotation(field: dict, annotation: str, where: str) -> None:
    """`@enum-ref <enum>` or `@id <kind> [<class field>]` on the payload field before it."""
    words = annotation.split()
    if words[0] == "enum-ref" and len(words) == 2:
        if words[1] not in PUBLISHED_ENUMS:
            fail(f"{where}.{field['name']}: @enum-ref {words[1]} is not a published (//#ref-enum) enum")
        field["enum_ref"] = words[1]
    elif words[0] == "id" and len(words) in (2, 3):
        if words[1] not in ID_KINDS:
            fail(f"{where}.{field['name']}: unknown @id kind '{words[1]}' (known: {', '.join(sorted(ID_KINDS))})")
        if (words[1] in PARENT_KINDS) != (len(words) == 3):
            fail(f"{where}.{field['name']}: '@id <kind> <parent field>' is required for {', '.join(sorted(PARENT_KINDS))} and not allowed for other kinds")
        field["id"] = {"kind": words[1], **({"parent_field": words[2]} if len(words) == 3 else {})}
    else:
        fail(f"{where}.{field['name']}: can't read annotation '/*@{annotation}*/' (use @enum-ref <enum> or @id <kind>)")
    if "enum_ref" in field and "id" in field:
        fail(f"{where}.{field['name']}: use either @enum-ref or @id, not both")


def parse_message(tag: str, body: str, field_names: set[str]) -> tuple[dict | None, str | None]:
    """(message, reason it's missing) from a LOG_BODY_<tag> macro body."""
    calls = list(re.finditer(r"\bsnprintf\s*\(", body))
    if len(calls) != 1:
        return None, f"{len(calls)} snprintf calls"
    if re.search(r"\bif\s*\(|\?", re.sub(r'"(?:\\.|[^"\\])*"', '""', body)):
        return None, "conditional formatting"
    inside, _ = call_args(body, calls[0].end() - 1)
    args = split_top_level(inside)
    if len(args) < 3:
        return None, "snprintf without a format"
    literals = re.findall(r'"((?:\\.|[^"\\])*)"', args[2])
    if not literals or re.sub(r'"(?:\\.|[^"\\])*"', "", args[2]).strip():
        return None, "format is not a string literal"
    fmt = c_unescape("".join(literals))
    conversions = [c for c in CONVERSION_RE.findall(fmt) if c != "%%"]
    values = args[3:]
    if len(conversions) != len(values):
        fail(f"LOG_BODY_{tag}: {len(conversions)} conversions but {len(values)} arguments")
    out_args = []
    for value in values:
        expression = value.strip()
        while True:
            stripped = CAST_RE.sub("", expression)
            if stripped == expression:
                break
            expression = stripped
        if match := FIELD_REF_RE.match(expression):
            arg = {"field": match.group(1)}
        elif match := VIA_REF_RE.match(expression):
            arg = {"field": match.group(2), "via": match.group(1)}
        elif re.fullmatch(r"\w+", expression) and (local := re.search(rf"(\w+)\(\s*\(p\)->(\w+)\s*,\s*{re.escape(expression)}\b", body)):
            arg = {"field": local.group(2), "via": local.group(1)}
        else:
            return None, f"argument '{expression}' is not a payload field"
        if arg["field"] not in field_names:
            fail(f"LOG_BODY_{tag}: references unknown payload field '{arg['field']}'")
        out_args.append(arg)
    return {"format": fmt, "args": out_args}, None


def name_tables(texts: dict[Path, str], macros: dict[str, tuple[str, str, Path]]) -> dict[str, dict]:
    """Value → text tables of the helper functions and macros the messages pass fields through."""
    tables = {}
    for path, text in texts.items():
        for match in NAME_FUNCTION_RE.finditer(text):
            body, _ = call_args(text.replace("{", "(").replace("}", ")"), match.end() - 1)
            cases = CASE_RE.findall(body)
            default = DEFAULT_RE.search(body)
            # Only pure tables: every return is a string literal.
            if cases and not re.search(r"\breturn\s+[^\"]", body):
                tables[match.group("name")] = {
                    "values": {value: c_unescape(text_) for value, text_ in cases},
                    **({"default": c_unescape(default.group(1))} if default else {}),
                    "source_file": path.relative_to(PROJECT_ROOT).as_posix(),
                }
    for name, (params, body, path) in macros.items():
        if not re.fullmatch(r"\s*\w+\s*", params):
            continue
        cases = TERNARY_CASE_RE.findall(body)
        default = TERNARY_DEFAULT_RE.search(body.strip())
        if cases and default:
            tables[name] = {"values": {value: c_unescape(text_) for value, text_ in cases}, "default": c_unescape(default.group(1)), "source_file": path.relative_to(PROJECT_ROOT).as_posix()}
    return tables


def schema_id(tags: list[dict]) -> int:
    """SE_schema_id() (sys_error.c): FNV-1a over name bytes + NUL, then ID, then payload size, per tag in map order."""
    value = FNV_OFFSET
    for tag in tags:
        for byte in tag["name"].encode("ascii") + b"\0":
            value = ((value ^ byte) * FNV_PRIME) & 0xFFFFFFFF
        value = ((value ^ tag["id"]) * FNV_PRIME) & 0xFFFFFFFF
        value = ((value ^ tag["payload"]["size"]) * FNV_PRIME) & 0xFFFFFFFF
    return value


def esp_error_names() -> dict:
    """esp_err_t names from the ESP-IDF the firmware is built with: $IDF_PATH, else build/project_description.json."""
    idf_path = os.environ.get("IDF_PATH")
    if not idf_path and PROJECT_DESCRIPTION_PATH.exists():
        idf_path = json.loads(PROJECT_DESCRIPTION_PATH.read_text(encoding="utf-8")).get("idf_path")
    if not idf_path:
        fail("ESP-IDF not found: set IDF_PATH or build the firmware once (build/project_description.json names it)")
    table = Path(idf_path) / ESP_ERR_TABLE
    if not table.exists():
        fail(f"{table} not found (esp_err_t names)")
    codes = {}
    for match in ESP_ERR_ROW_RE.finditer(table.read_text(encoding="utf-8", errors="replace")):
        codes[match.group("value")] = {"name": match.group("name"), "description": match.group("description")}
    if len(codes) < 10:
        fail(f"{table}: only {len(codes)} esp_err_t rows parsed; has the table format changed?")
    return {"source_file": f"$IDF_PATH/{ESP_ERR_TABLE.as_posix()}", "codes": codes}


def contract_features() -> dict:
    """Feature names per device contract, from the //@contract-features tables (feature ID = index)."""
    scanned = generate_enums.scan(generate_enums.COMPONENTS_DIR)
    symbols = scanned["symbols"]
    members = scanned["enums"][CONTRACT_ENUM]["members"]
    contracts: dict[str, dict] = {}
    for path in sorted(COMPONENTS_DIR.rglob("*.c")):
        text = path.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")
        for match in CONTRACT_FEATURES_RE.finditer(text):
            symbol = symbols.get(match.group("symbol"))
            if not symbol or symbol["enum"] != CONTRACT_ENUM:
                fail(f"{path.name}: //@contract-features ${match.group('symbol')} is not a {CONTRACT_ENUM} member")
            items = split_top_level(match.group("body"))
            if not items or items[-1] != "NULL":
                fail(f"{path.name}: {match.group('table')} must end with NULL")
            features = []
            for item in items[:-1]:
                literal = re.fullmatch(C_STRING, item)
                if not literal:
                    fail(f"{path.name}: {match.group('table')} entry '{item}' is not a string literal")
                features.append(c_unescape(literal.group(1)))
            key = str(symbol["value"])
            if key in contracts:
                fail(f"two //@contract-features tables for {match.group('symbol')}")
            contracts[key] = {"symbol": match.group("symbol"), "table": match.group("table"), "source_file": path.relative_to(PROJECT_ROOT).as_posix(), "features": features}
    missing = [m["name"] for m in members if not m["name"].endswith("_MAX") and str(m["value"]) not in contracts]
    if missing:
        fail(f"no //@contract-features table for {', '.join(missing)}")
    return {"enum": CONTRACT_ENUM, "contracts": dict(sorted(contracts.items(), key=lambda item: int(item[0])))}


def load_stream_byte() -> int:
    config = dict(SDKCONFIG_RE.findall(SDKCONFIG_PATH.read_text(encoding="utf-8", errors="ignore"))) if SDKCONFIG_PATH.exists() else {}
    raw = config.get("CONFIG_TX_PACKET_CLASS_ERRORS")
    if raw is None:
        fail("CONFIG_TX_PACKET_CLASS_ERRORS not found in sdkconfig (error stream byte)")
    return int(raw, 0)


def parse_levels() -> list[dict]:
    match = LEVEL_ENUM_RE.search(logical_text(BASE_PATH))
    if not match:
        fail(f"{BASE_PATH.name}: se_level_e not found")
    levels = [{"name": name, "value": int(value), "alias": name.removeprefix("SE_LEVEL_").capitalize()} for name, value in LEVEL_MEMBER_RE.findall(match.group("body"))]
    if not levels:
        fail("se_level_e has no members")
    return levels


def build_document() -> dict:
    headers = find_headers()
    codes_text = logical_text(CODES_PATH)
    included = INCLUDE_RE.findall(CODES_PATH.read_text(encoding="utf-8", errors="replace"))
    macros: dict[str, tuple[str, str, Path]] = {}
    texts: dict[Path, str] = {}
    for name in included:
        path = headers.get(name)
        if not path:
            fail(f"{CODES_PATH.name} includes {name}, not found in any errors/ folder")
        texts[path] = logical_text(path)
        for match in DEFINE_RE.finditer(texts[path]):
            macros[match.group("name")] = (match.group("params"), match.group("body"), path)

    def map_body(member: str) -> tuple[str, Path]:
        if member not in macros:
            fail(f"map {member} is aggregated in {CODES_PATH.name} but defined in none of its includes")
        params, body, path = macros[member]
        if params.strip() != "X":
            fail(f"{path.name}: {member} is not an X-macro map")
        return body, path

    levels = parse_levels()
    level_values = {level["name"]: level["value"] for level in levels}
    rel = lambda path: path.relative_to(PROJECT_ROOT).as_posix()

    owners = []
    for member in aggregate_members(codes_text, "SYS_OWNER_MAP"):
        body, path = map_body(member)
        for entry in x_entries(body):
            if len(entry) != 3:
                fail(f"{path.name}: owner entry {entry} needs (name, id, \"name\")")
            owners.append({"name": entry[0], "id": int(entry[1], 0), "source_file": rel(path)})

    logged = set()
    for member in aggregate_members(codes_text, "SYS_ERROR_LOGGER_MAP"):
        body, _ = map_body(member)
        logged.update(entry[0] for entry in x_entries(body))

    tags = []
    for member in aggregate_members(codes_text, "SYS_ERROR_MAP"):
        body, path = map_body(member)
        for entry in x_entries(body):
            if len(entry) != 4:
                fail(f"{path.name}: tag entry {entry[:2]} needs (name, id, level, struct)")
            name, raw_id, level, struct_text = entry
            if level not in level_values:
                fail(f"{path.name}: {name} has unknown severity {level}")
            payload = parse_payload(struct_text, f"{path.name}:{name}")
            tag = {"name": name, "id": int(raw_id, 0), "level": level_values[level], "source_file": rel(path), "payload": payload}
            if name in logged:
                log_body = macros.get(f"LOG_BODY_{name}")
                if not log_body:
                    fail(f"{name} is in a logger map but has no LOG_BODY_{name}")
                message, reason = parse_message(name, log_body[1], {field["name"] for field in payload["fields"]})
                tag["message"] = message
                if reason:
                    tag["message_unavailable"] = reason
            else:
                tag["message"] = None
                tag["message_unavailable"] = "no LOG_BODY"
            tags.append(tag)

    for kind, items in (("tag", tags), ("owner", owners)):
        seen: dict[int, str] = {}
        for item in items:
            if item["id"] in seen:
                fail(f"{kind} ID 0x{item['id']:04X} used by {seen[item['id']]} and {item['name']}")
            if not 0 < item["id"] <= 0xFFFF:
                fail(f"{kind} {item['name']} ID 0x{item['id']:X} is outside 1..0xFFFF")
            seen[item["id"]] = item["name"]

    tables = name_tables(texts, macros)
    used = {arg["via"] for tag in tags if tag["message"] for arg in tag["message"]["args"] if "via" in arg}
    value_names = {name: tables[name] for name in sorted(used) if name in tables}

    return {
        "$schema": SCHEMA_REL_PATH,
        "schemaVersion": 1,
        "kind": "errors",
        "schema_id": schema_id(tags),
        "packet": {
            "stream": f"0x{load_stream_byte():02X}",
            "byte_order": "little",
            "header": [{"name": "node_count", "type": "uint8_t"}, {"name": "depth", "type": "uint8_t"}, {"name": "schema_id", "type": "uint32_t"}],
            "node": [{"name": "payload_length", "type": "uint8_t"}, {"name": "tag", "type": "uint16_t"}, {"name": "owner", "type": "uint16_t"}],
            "depth_corrupt": 255,
            "notes": "Nodes run from the outermost error to the root cause. depth > node_count: the chain was cut to fit the frame. depth 255: corrupt or over-depth chain.",
        },
        "levels": levels,
        "owners": owners,
        "tags": tags,
        "value_names": value_names,
        "id_kinds": ID_KINDS,
        "esp_errors": esp_error_names(),
        "contract_features": contract_features(),
    }


def validate(document: dict) -> None:
    if jsonschema is None:
        print("WARNING: jsonschema package not installed - skipping schema validation")
        return
    schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
    errors = sorted(jsonschema.Draft202012Validator(schema).iter_errors(document), key=lambda error: list(error.path))
    if errors:
        details = "\n".join(f"  - at {'/'.join(map(str, error.path)) or '<root>'}: {error.message}" for error in errors)
        fail(f"errors catalog failed schema validation:\n{details}")


def main() -> int:
    document = build_document()
    validate(document)
    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUT_PATH.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    with_message = sum(1 for tag in document["tags"] if tag["message"])
    print(
        f"Wrote {OUT_PATH.relative_to(PROJECT_ROOT)} ({len(document['tags'])} tags, {with_message} with a message; "
        f"{len(document['owners'])} owners; {len(document['value_names'])} value-name tables; schema ID 0x{document['schema_id']:08X})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
