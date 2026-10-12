"""The error catalog: every tag of every module's `ERR_*_MAP` X-macro under components/ as one JSON, so the app can read an error record.

  #define ERR_VM_MAP(X)                                                                                  \\
    X(ERR_VM_OBJ_BAD_TYPE, ERR_TAG(ERR_MOD_VM, 18), ERR_SEV_LOW, { uint8_t type; /*@enum-ref vm_obj_t_e*/ }) /* text */ \\
    ...
  ERR_MODULE_DEFINE(ERR_MOD_VM, "vm", ERR_VM_MAP)           (in a .c file: the module's name)
  #define ERR_MOD_VM 15u                                     (err_modules.h: the module's ID)

Read the same way the firmware reads them (components/core/errors/err_macros.h): `[tag u32 LE][sev u8][len u8][payload]` per node
of an error record (components/core/router/ROUTER.MD), tag = module << 16 | code. The payload is a packed struct: its fields are
listed with offset and size, a field comment `/*@enum-ref <enum>*/` names a published enum (enums.json) the value is a member of, and
`/*@id <kind>*/` says the value is an ID of that kind (device, error-tag, vm-block-type, ...). A trailing `/* text */` after the X(...)
entry is the tag's description. Nothing is derived from a name: the app shows `name` (the C identifier) and the description.

Refused: a tag whose module differs from the map's module, two tags with one ID, a module without ERR_MODULE_DEFINE or an ID, a field
type the generator does not know, an `@enum-ref` of an enum that is not published, a size over the error node (ERR_DATA_MAX is a Kconfig
value, 24+ bytes: a payload over 64 is refused here).

Output: data-structures/errors/errors.generated.json (LF endings, generated: do not edit)
  {"modules": [{id, name, symbol, source_file, tags: [{code, tag, name, severity, description?, size, fields: [...]}]}]}

Usage:
  python data-structures/auto-annotations/errors/generate-errors.py
"""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
COMPONENTS_DIR = ROOT / "components"
ENUMS_JSON = ROOT / "data-structures" / "enums.json"
OUT = ROOT / "data-structures" / "errors" / "errors.generated.json"
SCHEMA_VERSION = 2
SKIP_DIRS = {"build", "legacy"}
PAYLOAD_MAX = 64

SEVERITIES = {"ERR_SEV_NONE": 0, "ERR_SEV_LOW": 1, "ERR_SEV_MEDIUM": 2, "ERR_SEV_HIGH": 3, "ERR_SEV_CRITICAL": 4}
TYPES = {  # C type -> (wire type, size)
    "uint8_t": ("u8", 1), "int8_t": ("i8", 1), "uint16_t": ("u16", 2), "int16_t": ("i16", 2),
    "uint32_t": ("u32", 4), "int32_t": ("i32", 4), "esp_err_t": ("i32", 4), "float": ("f32", 4), "bool": ("u8", 1),
}

MOD_DEFINE_RE = re.compile(r"^#define\s+(ERR_MOD_\w+)\s+(\d+)u?\b", re.MULTILINE)
MODULE_DEFINE_RE = re.compile(r'^ERR_MODULE_DEFINE\(\s*(ERR_MOD_\w+)\s*,\s*"([^"]*)"\s*,\s*(\w+)\s*\)', re.MULTILINE)
MAP_RE = re.compile(r"^#define\s+(\w+_MAP)\(X\)((?:[^\n]*\\\n)*[^\n]*)", re.MULTILINE)
TAG_CALL_RE = re.compile(r"ERR_TAG\(\s*(ERR_MOD_\w+)\s*,\s*(\d+)\s*\)")


def fail(message):
    raise SystemExit(f"ERROR: {message}")


def rel(path):
    return path.relative_to(ROOT).as_posix()


def sources():
    for path in sorted(COMPONENTS_DIR.rglob("*")):
        if path.suffix in (".h", ".c") and not (set(path.relative_to(COMPONENTS_DIR).parts) & SKIP_DIRS):
            yield path


def balanced(text, start):
    """The text of the parenthesis that opens at `start` (without the parentheses) and the index after it."""
    depth = 0
    for i in range(start, len(text)):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                return text[start + 1:i], i + 1
    fail("unbalanced parenthesis in an error map")


def split_top(text):
    """Splits on commas outside parentheses and braces."""
    parts, depth, current = [], 0, ""
    for ch in text:
        if ch in "({":
            depth += 1
        elif ch in ")}":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append(current.strip())
            current = ""
        else:
            current += ch
    parts.append(current.strip())
    return parts


def parse_fields(body, context, enums):
    """`{ type a; /*@enum-ref e*/ type b; }`: a comment after a field's `;` annotates that field."""
    fields, offset = [], 0
    for piece in body.strip().strip("{}").split(";"):
        lead = re.match(r"(?:\s*/\*.*?\*/)*", piece, flags=re.DOTALL)[0]
        decl = piece[len(lead):].strip()
        for key, value in re.findall(r"/\*\s*@([a-z-]+)\s+([\w-]+)\s*\*/", lead):
            if not fields:
                fail(f"{context}: an annotation before the first field")
            if key == "enum-ref":
                if value not in enums:
                    fail(f"{context}: field {fields[-1]['name']} refers to '{value}', which is not a published enum")
                fields[-1]["enum_ref"] = value
            elif key == "id":
                fields[-1]["id_kind"] = value
            else:
                fail(f"{context}: unknown field annotation @{key}")
        if not decl:
            continue
        match = re.fullmatch(r"(?P<type>[\w ]+?)\s+(?P<name>\w+)(?:\[(?P<count>\d+)\])?", decl)
        if not match:
            fail(f"{context}: cannot read the field '{decl}'")
        ctype, name, count = match["type"], match["name"], match["count"]
        if ctype == "char" and count:
            wire, size = "chars", int(count)
        elif ctype in TYPES and not count:
            wire, size = TYPES[ctype]
        else:
            fail(f"{context}: field '{decl}' has a type the catalog does not know")
        fields.append({"name": name, "type": wire, "offset": offset, "size": size})
        offset += size
    if offset > PAYLOAD_MAX:
        fail(f"{context}: payload of {offset} bytes is over {PAYLOAD_MAX}")
    return fields, offset


def scan():
    enums = json.loads(ENUMS_JSON.read_text(encoding="utf-8"))["enums"] if ENUMS_JSON.exists() else {}
    ids, names, maps, mapfile = {}, {}, {}, {}
    for path in sources():
        text = path.read_text(encoding="utf-8", errors="replace")
        if path.name == "err_modules.h":
            ids.update({m[0]: int(m[1]) for m in MOD_DEFINE_RE.findall(text)})
        for m in MODULE_DEFINE_RE.finditer(text):
            if m[1] in names:
                fail(f"{rel(path)}: ERR_MODULE_DEFINE of {m[1]} twice")
            names[m[1]] = (m[2], m[3])
        for m in MAP_RE.finditer(text):
            maps[m[1]] = m[2].replace("\\\n", " ")
            mapfile[m[1]] = path
    modules = []
    for symbol, (name, map_name) in sorted(names.items(), key=lambda item: ids.get(item[0], 1 << 16)):
        if symbol not in ids:
            fail(f"{symbol} has no #define in err_modules.h")
        if map_name not in maps:
            fail(f"{symbol}: the map {map_name} is not defined in any header")
        body, tags, seen = maps[map_name], [], {}
        position = 0
        while True:
            found = re.search(r"\bX\(", body[position:])
            if not found:
                break
            call, position = balanced(body, position + found.start() + 1)
            tail = re.match(r"\s*/\*(?!@)(.*?)\*/", body[position:], flags=re.DOTALL)
            description = " ".join(tail[1].split()) if tail else None
            parts = split_top(call)
            if len(parts) < 4:
                fail(f"{map_name}: an entry has {len(parts)} arguments, expected 4")
            tag_name, tag_id, severity = parts[0], parts[1], parts[2]
            payload = ", ".join(parts[3:])
            tag = TAG_CALL_RE.fullmatch(tag_id)
            if not tag:
                fail(f"{tag_name}: the ID '{tag_id}' is not ERR_TAG(module, code)")
            if tag[1] != symbol:
                fail(f"{tag_name}: belongs to {tag[1]} but sits in the map of {symbol}")
            if severity not in SEVERITIES:
                fail(f"{tag_name}: unknown severity {severity}")
            code = int(tag[2])
            if code in seen:
                fail(f"{tag_name}: ID {code} is also {seen[code]}")
            seen[code] = tag_name
            fields, size = parse_fields(payload, tag_name, enums)
            entry = {"code": code, "tag": ids[symbol] << 16 | code, "name": tag_name, "severity": SEVERITIES[severity], "size": size, "fields": fields}
            if description:
                entry["description"] = description
            tags.append(entry)
        modules.append({"id": ids[symbol], "name": name, "symbol": symbol, "source_file": rel(mapfile[map_name]), "tags": sorted(tags, key=lambda t: t["code"])})
    return modules


def render():
    return json.dumps({"schemaVersion": SCHEMA_VERSION, "kind": "errors", "severities": SEVERITIES, "modules": scan()}, indent=2, ensure_ascii=False) + "\n"


if __name__ == "__main__":
    document = render()
    OUT.write_text(document, encoding="utf-8", newline="\n")
    data = json.loads(document)
    print(f"Wrote {rel(OUT)}: {len(data['modules'])} modules, {sum(len(m['tags']) for m in data['modules'])} tags")
