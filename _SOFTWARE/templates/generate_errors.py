"""Publishes the error catalog of the firmware as one JSON: components/core/errors/errors.json.

Reads ERR_MOD_* from components/core/errors/err_modules.h, and the ERR_MODULE_DEFINE and ERR_*_MAP macros from the headers
and sources under components/. The text a user reads comes from the module's `errors_<module>.json` next to the header of
its map (templates/error-texts.schema.json): a message template per error and what a field refers to where the field's
name does not say it. Enum groups named there are read from generated/common/enums.json (generate_enums.py runs first).

Output: components/core/errors/errors.json (generated: do not edit), checked by templates/errors.schema.json

Usage: python templates/generate_errors.py [--check]
"""
import json
import re
from functools import lru_cache

from _common import GENERATED, ROOT, fail, publish, read, rel, sources

OUT = ROOT / "components" / "core" / "errors" / "errors.json"
SCHEMA_VERSION = 4
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

# What an id field refers to (`id_kind`), published as `id_kinds`. A kind with a `scope` is numbered within an instance of
# that kind (a pin of a device): the field's `of` names the field that holds it, else the app takes the nearest one in the
# chain. `format: hex`: the number is shown in hex.
ID_KIND_INFO = {
    "device": {"alias": "Device", "description": "A device by its ID"},
    "device_type": {"alias": "Device type", "description": "A device type (type_id of a device descriptor)"},
    "device_op": {"alias": "Device command", "description": "A device command: contract << 8 | op", "format": "hex", "scope": "device"},
    "pin": {"alias": "Pin", "description": "A pin of a device", "scope": "device"},
    "channel": {"alias": "Channel", "description": "A channel of a device", "scope": "device"},
    "i2c_bus": {"alias": "I2C bus", "description": "An I2C bus"},
    "i2c_address": {"alias": "I2C address", "description": "A 7-bit address on an I2C bus", "format": "hex", "scope": "i2c_bus"},
    "vm_object": {"alias": "Variable", "description": "A VM object by its id: a variable of the program or one of its elements"},
    "vm_accessor": {"alias": "Accessor", "description": "A VM accessor by its id: the path a block reads or writes through"},
    "vm_block": {"alias": "Block", "description": "A VM block by its place in the execution order"},
    "vm_block_input": {"alias": "Block input", "description": "An input pin of a VM block, from 0", "scope": "vm_block"},
    "vm_block_output": {"alias": "Block output", "description": "An output pin of a VM block, from 0", "scope": "vm_block"},
    "vm_block_enable": {"alias": "Block enable", "description": "An enable source of a VM block, from 0", "scope": "vm_block"},
    "vm_block_type": {"alias": "Block type", "description": "A VM block type (wire_id of a block descriptor)"},
    "vm_op": {"alias": "Program command", "description": "A command of the VM router"},
    "action": {"alias": "Action", "description": "A stored action by its ID"},
    "project_section": {"alias": "Project section", "description": "A section of the stored project by its ID"},
    "router_key": {"alias": "Command target", "description": "A router key"},
    "link": {"alias": "Link", "description": "A router link (a transport connection)"},
    "connector": {"alias": "Connector", "description": "A data connector by its ID"},
    "transport": {"alias": "Transport", "description": "A transport provider"},
    "ble_uuid": {"alias": "BLE characteristic", "description": "A 16-bit BLE UUID", "format": "hex"},
    "stream": {"alias": "Stream", "description": "A stream byte"},
    "contract": {"alias": "Contract", "description": "A device contract by its ID"},
    "error_tag": {"alias": "Error", "description": "An error tag"},
    "esp_err": {"alias": "ESP-IDF error", "description": "An esp_err_t code", "format": "hex"},
}

# field name -> id kind, for every module, then per module (what the number means to the app); errors_<module>.json and
# `/* @id kind */` notes override them
ID_KINDS = {"pin": "pin", "source_pin": "pin", "bus": "i2c_bus", "provider": "transport", "stream": "stream", "contract": "contract", "tag": "error_tag", "root_tag": "error_tag"}
MODULE_ID_KINDS = {
    "ERR_MOD_SYS_I2C": {"addr": "i2c_address"},
    "ERR_MOD_SYS_DEVICE": {"id": "device", "user": "device", "type": "device_type", "op": "device_op"},
    "ERR_MOD_SYS_IO": {"id": "device"},
    "ERR_MOD_SYS_POWER": {"id": "device", "channel": "channel"},
    "ERR_MOD_SYS_POWER_MANAGER": {"id": "device", "source": "device", "channel": "channel"},
    "ERR_MOD_SYS_EVENT": {"source": "device", "device": "device", "channel": "channel"},
    "ERR_MOD_ROUTER": {"key": "router_key", "link": "link"},
    "ERR_MOD_SYS_CONNECTOR": {"id": "connector"},
    "ERR_MOD_SYS_ACTIONS": {"id": "action"},
    "ERR_MOD_SYS_PROJECT": {"id": "project_section"},
    "ERR_MOD_SYS_BLE": {"uuid": "ble_uuid"},
    "ERR_MOD_VM": {"obj_id": "vm_object", "parent_id": "vm_object", "acc_id": "vm_accessor", "block_idx": "vm_block", "blk_id": "vm_block", "packet": "vm_op", "device_id": "device"},
}
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
    if name in MODULE_ID_KINDS.get(module, {}):
        return MODULE_ID_KINDS[module][name]
    if name in ID_KINDS:
        return ID_KINDS[name]
    if ctype == "esp_err_t" or (name == "code" and "ESP" in tag):
        return "esp_err"
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
        if SEVERITIES[severity] == SEVERITIES["ERR_SEV_NONE"]:
            entry["context"] = True
        tags.append(entry)


PLACEHOLDER_RE = re.compile(r"\{([^{}]*)\}")


@lru_cache(maxsize=None)
def enums():
    path = GENERATED / "common" / "enums.json"
    if not path.exists():
        fail(f"{rel(path)} is missing: run generate_enums.py first")
    return json.loads(read(path))["enums"]


def apply_texts(module, tags, path):
    """Merges errors_<module>.json (the message templates, and what fields refer to) into the module's tags."""
    if not path.exists():
        fail(f"{module['symbol']}: {rel(path)} is missing (a message template for every error)")
    doc = json.loads(read(path))
    where = rel(path)
    if doc.get("module") != module["symbol"]:
        fail(f"{where}: module is {doc.get('module')}, the map is {module['symbol']}")
    texts = doc.get("errors", {})
    by_name = {t["name"]: t for t in tags}
    for name in texts:
        if name not in by_name:
            fail(f"{where}: {name} is not an error of {module['symbol']}")
    for tag in tags:
        text = texts.get(tag["name"])
        if not text or not text.get("message", "").strip():
            fail(f"{where}: {tag['name']} has no message")
        fields = {f["name"]: f for f in tag["fields"]}
        for name, override in text.get("fields", {}).items():
            field = fields.get(name)
            if not field:
                fail(f"{where}: {tag['name']} has no field {name}")
            for key in ("id_kind", "of", "enum_ref", "unit"):
                if key in override:
                    field[key] = override[key]
            if override.get("id_kind") is None and "id_kind" in override:
                del field["id_kind"]
            if "id_kind_by" in override:
                by = override["id_kind_by"]
                field.pop("id_kind", None)
                selector = fields.get(by["field"])
                if not selector:
                    fail(f"{where}: {tag['name']}.{name}: id_kind_by names {by['field']}, which is not a field")
                group = selector.get("enum_ref")
                # by member name of the selector's enum group, or by number when it has none (a flag)
                members = {m["name"]: m["value"] for m in enums().get(group, {}).get("members", [])} if group else {}
                kinds = {}
                for member, kind in by["kinds"].items():
                    if not group and member.isdigit():
                        members[member] = int(member)
                    if member not in members:
                        fail(f"{where}: {tag['name']}.{name}: {member} is not " + (f"a member of {group}" if group else "a number (the selector has no enum_ref)"))
                    if kind not in ID_KIND_INFO:
                        fail(f"{where}: {tag['name']}.{name}: unknown id kind {kind}")
                    kinds[str(members[member])] = kind
                field["id_kind_by"] = {"field": by["field"], "kinds": kinds}
        for field in tag["fields"]:
            kind = field.get("id_kind")
            if kind and kind not in ID_KIND_INFO:
                fail(f"{where}: {tag['name']}.{field['name']}: unknown id kind {kind}")
            if field.get("enum_ref") and field["enum_ref"] not in enums():
                fail(f"{where}: {tag['name']}.{field['name']}: enum group {field['enum_ref']} is not published")
            scope = ID_KIND_INFO.get(kind, {}).get("scope")
            if "of" in field:
                holder = fields.get(field["of"])
                if not holder or holder.get("id_kind") != scope:
                    fail(f"{where}: {tag['name']}.{field['name']}: of = {field['of']} is not a {scope} field of the error")
            elif scope:
                holders = [f["name"] for f in tag["fields"] if f.get("id_kind") == scope]
                if len(holders) > 1:
                    fail(f"{where}: {tag['name']}.{field['name']}: a {kind} of which {scope} ({', '.join(holders)})? Set `of`")
                if holders:
                    field["of"] = holders[0]
        message = " ".join(text["message"].split())
        for placeholder in PLACEHOLDER_RE.findall(message):
            if placeholder not in fields:
                fail(f"{where}: {tag['name']}: message names {{{placeholder}}}, which is not a field ({', '.join(fields) or 'none'})")
        tag["message"] = message
        if text.get("hint", "").strip():
            hint = " ".join(text["hint"].split())
            for placeholder in PLACEHOLDER_RE.findall(hint):
                if placeholder not in fields:
                    fail(f"{where}: {tag['name']}: hint names {{{placeholder}}}, which is not a field ({', '.join(fields) or 'none'})")
            tag["hint"] = hint


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
        module = {
            "id": ids[symbol],
            "name": name,
            "symbol": symbol,
            "source_file": rel(map_file[map_name]),
            "texts_file": rel(map_file[map_name].parent / f"errors_{name}.json"),
            "tags": parse_map(maps[map_name], symbol, ids[symbol], map_name),
        }
        apply_texts(module, module["tags"], map_file[map_name].parent / f"errors_{name}.json")
        modules.append(module)
    known = {m["texts_file"] for m in modules}
    for path in sources(".json"):
        if path.name.startswith("errors_") and rel(path) not in known:
            fail(f"{rel(path)}: no error module has its map next to it under that name")
    return modules


def main():
    publish(OUT, {
        "$schema": "../../../templates/errors.schema.json",
        "schemaVersion": SCHEMA_VERSION,
        "kind": "errors",
        "severities": SEVERITIES,
        "id_kinds": ID_KIND_INFO,
        "modules": scan(),
    }, "generate_errors.py")


if __name__ == "__main__":
    main()
