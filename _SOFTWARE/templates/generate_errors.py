"""Publishes the error catalog of the firmware as one JSON: components/core/errors/errors.json.

Reads ERR_MOD_* from components/core/errors/err_modules.h, and the ERR_MODULE_DEFINE and ERR_*_MAP macros from the headers
and sources under components/.

Output: components/core/errors/errors.json (generated: do not edit), checked by templates/errors.schema.json

Usage: python templates/generate_errors.py [--check]
"""
import re

from _common import ROOT, fail, publish, read, rel, sources

OUT = ROOT / "components" / "core" / "errors" / "errors.json"
SCHEMA_VERSION = 3
PAYLOAD_MAX = 64

SEVERITIES = {"ERR_SEV_NONE": 0, "ERR_SEV_LOW": 1, "ERR_SEV_MEDIUM": 2, "ERR_SEV_HIGH": 3, "ERR_SEV_CRITICAL": 4}

TYPES = {
    "uint8_t": ("u8", 1),
    "int8_t": ("i8", 1),
    "uint16_t": ("u16", 2),
    "int16_t": ("i16", 2),
    "uint32_t": ("u32", 4),
    "int32_t": ("i32", 4),
    "esp_err_t": ("i32", 4),
    "float": ("f32", 4),
    "bool": ("u8", 1),
}

# field name -> id kind (what the number means to the app)
ID_KINDS = {"pin": "pin", "source_pin": "pin", "bus": "i2c_bus", "provider": "transport", "stream": "stream", "contract": "contract", "tag": "error_tag", "packet": "error_tag"}
DEVICE_ID_MODULES = {"ERR_MOD_SYS_DEVICE", "ERR_MOD_SYS_IO", "ERR_MOD_SYS_POWER", "ERR_MOD_SYS_POWER_MANAGER"}
DEVICE_ID_FIELDS = {"id", "target", "device_id", "vreg"}
UNITS = {"_Hz": "Hz", "_mV": "mV", "_mA": "mA", "_ms": "ms", "_us": "us"}  # field name suffix -> unit

MOD_DEFINE_RE = re.compile(r"^#define\s+(ERR_MOD_\w+)\s+(\d+)u?\b", re.MULTILINE)
MODULE_DEFINE_RE = re.compile(r'^ERR_MODULE_DEFINE\(\s*(ERR_MOD_\w+)\s*,\s*"([^"]*)"\s*,\s*(\w+)\s*\)', re.MULTILINE)
MAP_RE = re.compile(r"^#define\s+(\w+_MAP)\(X\)((?:[^\n]*\\\n)*[^\n]*)", re.MULTILINE)
TAG_CALL_RE = re.compile(r"ERR_TAG\(\s*(ERR_MOD_\w+)\s*,\s*(\d+)\s*\)")
FIELD_RE = re.compile(r"(?P<type>[\w ]+?)\s+(?P<name>\w+)(?:\[(?P<count>\d+)\])?")
NOTE_RE = re.compile(r"/\*\s*@([a-z-]+)\s+([\w-]+)\s*\*/")


def balanced(text, start):
    """(the text inside the parentheses opened at `start`, the index after the closing one)."""
    depth = 0
    for i in range(start, len(text)):
        depth += (text[i] == "(") - (text[i] == ")")
        if depth == 0:
            return text[start + 1:i], i + 1
    fail("unbalanced parenthesis in error map")


def split_top(text):
    """Splits at the commas that are outside parentheses and braces."""
    parts, depth, current = [], 0, ""
    for ch in text:
        depth += (ch in "({") - (ch in ")}")
        if ch == "," and depth == 0:
            parts.append(current.strip())
            current = ""
        else:
            current += ch
    return parts + [current.strip()]


def id_kind(name, ctype, module, tag):
    if name in ID_KINDS:
        return ID_KINDS[name]
    if ctype == "esp_err_t" or (name == "code" and "ESP" in tag):
        return "esp_err"
    if module in DEVICE_ID_MODULES and name in DEVICE_ID_FIELDS:
        return "device"
    return None


def parse_fields(body, context, module, tag):
    """([field], payload size) of a map entry's `{ type name; ... }`; `/* @id kind */` and `/* @enum-ref name */` annotate a field."""
    fields, offset = [], 0
    for piece in body.strip().strip("{}").split(";"):
        lead = re.match(r"(?:\s*/\*.*?\*/)*", piece, flags=re.DOTALL)[0]
        decl = piece[len(lead):].strip()

        # an annotation after a field belongs to it; before the first field it belongs to the next declaration
        notes = {"id": None, "enum-ref": None}
        for key, value in NOTE_RE.findall(lead):
            if key not in notes:
                continue
            if fields:
                fields[-1]["id_kind" if key == "id" else "enum_ref"] = value.replace("-", "_") if key == "id" else value
            else:
                notes[key] = value.replace("-", "_") if key == "id" else value
        if not decl:
            continue

        match = FIELD_RE.fullmatch(decl)
        if not match:
            fail(f"{context}: cannot parse field declaration '{decl}'")
        ctype, name, count = match["type"].strip(), match["name"], match["count"]
        if ctype == "char" and count:
            wire, size = "chars", int(count)
        elif ctype in TYPES and not count:
            wire, size = TYPES[ctype]
        else:
            fail(f"{context}: field '{decl}' has unsupported type '{ctype}'")

        field = {"name": name, "type": wire, "offset": offset, "size": size}
        kind = notes["id"] or id_kind(name, ctype, module, tag)
        if kind:
            field["id_kind"] = kind
        if notes["enum-ref"]:
            field["enum_ref"] = notes["enum-ref"]
        unit = next((u for suffix, u in UNITS.items() if name.endswith(suffix)), None)
        if unit:
            field["unit"] = unit
        fields.append(field)
        offset += size

    if offset > PAYLOAD_MAX:
        fail(f"{context}: payload of {offset} bytes exceeds limit of {PAYLOAD_MAX}")
    return fields, offset


def parse_map(body, symbol, module_id, map_name):
    """The tags of one ERR_*_MAP: `X(NAME, ERR_TAG(module, code), severity, { payload })` with an optional `/* description */`."""
    tags, seen, position = [], {}, 0
    while True:
        found = re.search(r"\bX\(", body[position:])
        if not found:
            return sorted(tags, key=lambda t: t["code"])
        call, position = balanced(body, position + found.start() + 1)
        tail = re.match(r"\s*/\*(?!@)(.*?)\*/", body[position:], flags=re.DOTALL)
        parts = split_top(call)
        if len(parts) < 4:
            fail(f"{map_name}: entry has {len(parts)} arguments, expected 4")
        name, tag_id, severity = parts[:3]

        tag = TAG_CALL_RE.fullmatch(tag_id)
        if not tag:
            fail(f"{name}: ID '{tag_id}' is not ERR_TAG(module, code)")
        if tag[1] != symbol:
            fail(f"{name}: belongs to {tag[1]} but sits in map of {symbol}")
        if severity not in SEVERITIES:
            fail(f"{name}: unknown severity {severity}")
        code = int(tag[2])
        if code in seen:
            fail(f"{name}: code {code} already used by {seen[code]}")
        seen[code] = name

        fields, size = parse_fields(", ".join(parts[3:]), name, symbol, name)
        entry = {"code": code, "tag": (module_id << 16) | code, "name": name, "severity": SEVERITIES[severity], "size": size, "fields": fields}
        if tail and tail[1].strip():
            entry["description"] = " ".join(tail[1].split())
        tags.append(entry)


def scan():
    ids, names, maps, map_file = {}, {}, {}, {}
    for path in sources(".h", ".c"):
        text = read(path)
        if path.name == "err_modules.h":
            ids.update({m[0]: int(m[1]) for m in MOD_DEFINE_RE.findall(text)})
        for m in MODULE_DEFINE_RE.finditer(text):
            if m[1] in names:
                fail(f"{rel(path)}: duplicate ERR_MODULE_DEFINE for {m[1]}")
            names[m[1]] = (m[2], m[3])
        for m in MAP_RE.finditer(text):
            maps[m[1]] = m[2].replace("\\\n", " ")
            map_file[m[1]] = path

    modules = []
    for symbol, (name, map_name) in sorted(names.items(), key=lambda item: ids.get(item[0], 1 << 16)):
        if symbol not in ids:
            fail(f"{symbol} has no #define in err_modules.h")
        if map_name not in maps:
            fail(f"{symbol}: map {map_name} not found in headers")
        modules.append({
            "id": ids[symbol],
            "name": name,
            "symbol": symbol,
            "source_file": rel(map_file[map_name]),
            "tags": parse_map(maps[map_name], symbol, ids[symbol], map_name),
        })
    return modules


def main():
    publish(OUT, {
        "$schema": "../../../templates/errors.schema.json",
        "schemaVersion": SCHEMA_VERSION,
        "kind": "errors",
        "severities": SEVERITIES,
        "modules": scan(),
    }, "generate_errors.py")


if __name__ == "__main__":
    main()
