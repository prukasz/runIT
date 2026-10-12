"""Generate the app's block descriptors from the block headers: one JSON per block, a palette index and the user guides.

Grammar and rules: vm-block-annotations.md. In short:

  * A block is a folder `components/vm/vm_blocks/<group>/<name>/` holding `vm_block_<name>.h` (annotated), `vm_block_<name>.c`
    (the body and `VM_BLOCK_TYPE_DEFINE`) and `<name>.md` (the user guide, written by hand).
  * Everything the app shows or needs comes from the header and the .c, nothing from a second file: the block's user name
    (`@alias`), its description, the pins, the settings, the enums and their members, the face line, and for a device block
    the operations of its table. Every name a user sees is an explicit `@alias`.
  * The firmware's shape (pin counts, required pins, state size) is read from `VM_BLOCK_TYPE_DEFINE` and checked against the pins.
  * Anything not understood is an error, so a typo never silently drops a fact.

Output, in data-structures/vm/blocks/ (LF line endings, nothing generated is edited by hand):
  <name>.block.json     what the block is and how it looks
  <name>.md             the user guide, copied
  index.generated.json  the palette: every block, and the shared vocabularies (value kinds, activation kinds, id kinds, ...)

Usage:
  python data-structures/auto-annotations/vm/generate-vm-blocks.py [--check]
"""
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
VM_DIR = ROOT / "components" / "vm"
BLOCKS_DIR = VM_DIR / "vm_blocks"
IDS_HEADER = VM_DIR / "vm_core" / "block" / "vm_block_ids.h"
COMPONENTS_DIR = ROOT / "components"
OUT_DIR = ROOT / "data-structures" / "vm" / "blocks"
SDKCONFIG = ROOT / "sdkconfig"
SCHEMA_VERSION = 2

# ---------------------------------------------------------------------------
# Vocabularies (also published in the index)
# ---------------------------------------------------------------------------

VALUE_KINDS = {
    "bool": "Read as true when non-zero / written 1 or 0. Any scalar object.",
    "u8": "Read or written as an unsigned 8-bit number. Any scalar object (converted).",
    "u32": "Read or written as an unsigned 32-bit number. Any scalar object (converted).",
    "i32": "Read or written as a signed 32-bit number. Any scalar object (converted).",
    "f32": "Read or written as a float. Any scalar object (converted).",
    "scalar": "Read in its own type; the block's behaviour depends on it (see the block). B, U8, U32, I32 or F.",
    "gate": "Written 1 while active and cleared quietly (0 without marking it updated) otherwise: wire it to enables.",
    "object": "A whole object or tree; source and destination must have the same types and counts.",
    "ptr-cell": "An element of a PTR object; the block hangs its own tree there.",
}
ACTIVATION_KINDS = {
    "enabled": "Runs every pass while its enables allow it (always, with no enables); resets or clears its outputs when disabled.",
    "triggered": "Runs when an input it watches is fresh (marked updated this pass) and it is enabled.",
    "enable-rising": "Runs once each time its enables turn it on.",
}
ID_KINDS = {
    "device": "The id (u16) of a device created in the project. `contract` names the contract the device must answer, `device_type` the device type that has operations of its own.",
    "pin": "An index of an IO resource of the device in the field named by `device_field` (a pin, a channel: the alias says which).",
    "channel": "The channel (pin or channel) an event carries, of the device in `device_field`, as that device declares it.",
    "event": "The event of the device in `device_field`: the device's own events, or any event when no device is chosen.",
    "action": "A record ID (u32) of the action store (SYS_ACTIONS.MD).",
}
OP_KINDS = {
    "change": "A setting: sent on the first enabled pass and whenever an argument changed (write-on-change).",
    "edge": "An action (toggle, brake, reset): sent once per rising edge of the enable.",
    "read": "A reading: asked on every enabled pass, the values written to the outputs in order.",
}
LOOP_USES = {"refuse": "refused", "once": "once-per-turn"}
FIELD_ROLES = {
    "device": "The device the block works on (an id field with a contract or device type).",
    "op": "The operation the block performs: one of `ops`; it decides the pins.",
    "idle-op": "The operation sent once when the block is not enabled (0 = none): one of `ops` that is not a reading.",
    "const": "The constant of input `i` of the chosen operation, used while that input is unwired (`i` = the field's array index).",
    "idle-const": "The constants of the idle operation's inputs after its address inputs.",
}
COMMON_RULES = [
    {"rule": "The pin counts, the required pins and the size of the private state match the block's palette entry.", "error": "ERR_VM_BLK_BAD_SHAPE"},
]

TYPES = {  # (size, alignment) of the C types a state struct may use; others: `//#vm-type <name> @size <n> @align <n>`
    "uint8_t": (1, 1), "int8_t": (1, 1), "bool": (1, 1), "char": (1, 1),
    "uint16_t": (2, 2), "int16_t": (2, 2),
    "uint32_t": (4, 4), "int32_t": (4, 4), "float": (4, 4),
    "uint64_t": (8, 8), "int64_t": (8, 8), "double": (8, 8),
}
OVERRIDE_TYPES = {  # the C types a pin of each value kind may take its constant from
    "bool": {"uint8_t", "bool"}, "u8": {"uint8_t"}, "u32": {"uint32_t", "uint16_t", "uint8_t"},
    "i32": {"int32_t", "int16_t", "int8_t"}, "f32": {"float"}, "scalar": set(TYPES), "object": {"float"},
}
DEV_ACTS = {"CHANGE": "change", "EDGE": "edge", "READ": "read"}
DEV_FIELD_TYPES = {"U8": ("u8", 1), "I16": ("i16", 2), "U32": ("u32", 4), "I32": ("i32", 4)}
DEV_RESULTS = {"NONE": ("none", 0, 0), "U8": ("u8", 1, 1), "I32": ("i32", 1, 4), "I32X2": ("i32", 2, 4),
               "U8X2": ("u8", 2, 1), "U8X4": ("u8", 4, 1)}  # name -> (element type, count, element bytes)

# Tags per line kind. A tag outside its set, or given twice, is an error.
BLOCK_TAGS = {  # `//@<tag> <text>` lines of the block directive: kind of value, many allowed
    "alias": "one", "category": "one", "description": "one", "activation": "one", "when": "one", "face": "one",
    "enables": "one", "loop": "one", "data": "one", "data-tail": "one", "opcodes": "one", "formula": "one",
    "contract": "one", "device-type": "one", "en-alias": "one", "en-description": "one", "en-signal": "one",
    "en-connect": "one", "eno-alias": "one", "eno-description": "one", "rule": "many", "example": "many",
}
REQUIRED_BLOCK_TAGS = ("alias", "category", "description", "activation", "when")
PIN_TAGS = {"alias", "description", "value", "required", "overrides", "repeat", "user-write", "id", "device-field", "spec",
            "hide-when-wired", "unit"}
FIELD_TAGS = {"alias", "description", "runtime", "derived", "enum-ref", "id", "device-field", "contract", "device-type",
              "source-field", "any", "min", "max", "unit", "hidden-by-default", "let-user-select-available", "dynamic-input", "role"}
MEMBER_TAGS = {"alias", "description", "format", "stack-op", "wire-size", "mem-size"}  # wire-size / mem-size are the enums generator's (element bytes), accepted here and not used
RULE_TAGS = {"error"}
EXAMPLE_TAGS = {"in", "consts", "code", "result"}
OP_TAGS = {"alias", "description", "face"}
OP_PIN_TAGS = {"alias", "description", "id", "unit", "min", "max", "choices"}
ENUM_MARK_TAGS = {"alias"}
FLAG_TAGS = {"required", "repeat", "user-write", "hide-when-wired", "runtime", "hidden-by-default", "stack-op"}

TAG_RE = re.compile(r"(?:^|(?<=\s))@([a-z][a-z0-9-]*)(?=\s|$)")
DIRECTIVE_RE = re.compile(r"^//@(?P<keyword>[a-z][a-z0-9-]*)(?:[ \t]+(?P<rest>.*))?$")
BLOCK_START_RE = re.compile(r"^//#block\s+(VM_BLK_\w+)\s*$")
REF_RE = re.compile(r"\{(\w+)\}")
IDENT_RE = re.compile(r"^[A-Za-z_]\w*$")
FIELD_RE = re.compile(r"^\s*(?P<type>[A-Za-z_][\w ]*?)\s+(?P<name>\w+)\s*(?:\[(?P<arr>\w*)\])?\s*;\s*(?://\s*(?P<comment>.*))?$")
ENUM_RE = re.compile(r"(?P<marker>//#(?P<kind>block-enum|ref-enum)[^\n]*\n\s*)?typedef\s+enum(?:\s+(?P<tag>\w+))?\s*\{(?P<body>.*?)\}\s*(?P<name>\w+)\s*;", re.DOTALL)
MEMBER_RE = re.compile(r"^(?P<name>\w+)\s*(?:=\s*(?P<value>[^,/]+?))?\s*,?\s*(?://\s*(?P<comment>.*))?$")
VM_TYPE_RE = re.compile(r"^//#vm-type\s+(\w+)\s+@size\s+(\d+)\s+@align\s+(\d+)\s*$", re.MULTILINE)
DEFINE_RE = re.compile(r"^#define\s+(\w+)\s+([^\n]+?)\s*(?://.*)?$", re.MULTILINE)
OPENTRY_RE = re.compile(r"\[(?P<sym>\w+)\]\s*=\s*\{\s*(?P<pops>\d+)\s*,\s*(?P<pushes>\d+)\s*,\s*VM_\w*?ARG_(?P<arg>NONE|INPUT|CONST)\s*\}")
ERROR_RE = re.compile(r"X\(\s*(ERR_\w+)\s*,\s*ERR_TAG\(\s*(ERR_MOD_\w+)\s*,\s*(\d+)\s*\)")
MODULE_RE = re.compile(r"^#define\s+(ERR_MOD_\w+)\s+(\d+)u?\b", re.MULTILINE)
GUIDE_NAME = "{name}.md"


def fail(message):
    raise SystemExit(f"ERROR: {message}")


# ---------------------------------------------------------------------------
# Reading C
# ---------------------------------------------------------------------------

_CACHE = {}


def read(path):
    key = str(path)
    if key not in _CACHE:
        _CACHE[key] = path.read_text(encoding="utf-8", errors="ignore").replace("\r\n", "\n")
    return _CACHE[key]


def rel(path):
    return path.relative_to(ROOT).as_posix()


def vm_headers():
    if "headers" not in _CACHE:
        _CACHE["headers"] = sorted([*VM_DIR.rglob("*.h"), *COMPONENTS_DIR.joinpath("sys").rglob("*.h"),
                                    *COMPONENTS_DIR.joinpath("devices").rglob("*.h")])
    return _CACHE["headers"]


def parse_tags(text, allowed, context, flags=FLAG_TAGS):
    """(lead, tags): the text before the first tag, and `{tag: value}`. An unknown or repeated tag fails."""
    matches = list(TAG_RE.finditer(text))
    lead = (text[:matches[0].start()] if matches else text).strip()
    tags = {}
    for i, m in enumerate(matches):
        name = m.group(1)
        if name not in allowed:
            fail(f"{context}: unknown tag @{name} (this line takes: {', '.join('@' + t for t in sorted(allowed))})")
        if name in tags:
            fail(f"{context}: @{name} given twice")
        end = matches[i + 1].start() if i + 1 < len(matches) else len(text)
        value = text[m.end():end].strip()
        if name in flags and value:
            fail(f"{context}: @{name} takes no value (got `{value}`)")
        tags[name] = value
    return lead, tags


def safe_int(expr, context):
    """An integer C constant expression (`0x1u`, `(1u << 0) | (1u << 2)`); nothing else."""
    text = re.sub(r"(?<=[0-9a-fA-F])[uUlL]+\b", "", expr.strip())
    if not text or not re.fullmatch(r"[0-9a-fA-FxX<>|&+()\s*/-]+", text):
        fail(f"{context}: cannot read `{expr.strip()}` as an integer constant")
    try:
        return int(eval(text, {"__builtins__": {}}, {}))  # noqa: S307 - the character set above admits only numbers and operators
    except Exception:
        fail(f"{context}: cannot read `{expr.strip()}` as an integer constant")


def split_top_level(text):
    """Split on commas outside brackets."""
    items, depth, start = [], 0, 0
    for i, ch in enumerate(text):
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        elif ch == "," and depth == 0:
            items.append(text[start:i])
            start = i + 1
    items.append(text[start:])
    return [item.strip() for item in items if item.strip()]


def sdkconfig():
    if "sdkconfig" not in _CACHE:
        if not SDKCONFIG.exists():
            fail(f"{SDKCONFIG} not found: configure the firmware once (idf.py reconfigure) so the Kconfig limits can be read")
        out = {}
        for line in read(SDKCONFIG).splitlines():
            m = re.match(r"^(CONFIG_\w+)=(0x[0-9a-fA-F]+|-?\d+)$", line)
            if m:
                out[m.group(1)] = int(m.group(2), 0)
        _CACHE["sdkconfig"] = out
    return _CACHE["sdkconfig"]


def defines():
    """Every `#define NAME value` of the VM, sys and device headers (object-like macros only)."""
    if "defines" not in _CACHE:
        found = {}
        for header in vm_headers():
            for name, value in DEFINE_RE.findall(read(header)):
                if "(" not in name:
                    found.setdefault(name, value)
        _CACHE["defines"] = found
    return _CACHE["defines"]


def symbol_value(name, context, seen=()):
    """The integer a name stands for: a Kconfig option (from sdkconfig) or a `#define` of a header (followed through other names)."""
    if name in sdkconfig():
        return sdkconfig()[name]
    if name in seen:
        fail(f"{context}: {name} refers to itself")
    value = defines().get(name)
    if value is None:
        fail(f"{context}: {name} is neither a Kconfig option nor a #define of a VM / sys / device header")
    return eval_c(value, context, seen + (name,))


def eval_c(expr, context, seen=()):
    """An integer C expression: numbers, names (symbol_value) and `sizeof(<struct>)` of a state struct."""
    def sizeof(m):
        return str(struct_layout(m.group(1), context)["size"])
    text = re.sub(r"sizeof\(\s*(\w+)\s*\)", sizeof, expr)
    text = re.sub(r"\b([A-Za-z_]\w*)\b", lambda m: str(symbol_value(m.group(1), context, seen)), text)
    return safe_int(text, context)


# ---------------------------------------------------------------------------
# Enums
# ---------------------------------------------------------------------------

def parse_members(body, context):
    """The members of an enum body: `{name, value, alias, description, format, stack_op}`; comment-only lines are ignored."""
    members, value = [], -1
    for raw in body.split("\n"):
        line = raw.strip()
        if not line or line.startswith("//"):
            continue
        m = MEMBER_RE.match(line)
        if not m:
            fail(f"{context}: cannot parse enum member `{line}`")
        if m.group("value"):
            value = eval_c(m.group("value"), f"{context}: {m.group('name')}")
        else:
            value += 1
        member = {"name": m.group("name"), "value": value}
        lead, tags = parse_tags(m.group("comment") or "", MEMBER_TAGS, f"{context}: {m.group('name')}")
        if lead:
            fail(f"{context}: {m.group('name')}: unexpected text `{lead}` before the first tag (use @alias / @description)")
        for key in ("alias", "description", "format"):
            if key in tags:
                if not tags[key]:
                    fail(f"{context}: {m.group('name')}: @{key} is empty")
                member[key] = tags[key]
        if "stack-op" in tags:
            member["stack_op"] = True
        members.append(member)
    return members


def scan_enums(path):
    """{name: {alias, kind, members, source_file}} of the enums a header declares: `//#block-enum` / `//#ref-enum` ones carry an
    alias; the others are kept (pin enums, opcode enums) with `kind` None."""
    found = {}
    text = read(path)
    for m in ENUM_RE.finditer(text):
        name = m.group("name")
        if not m.group("marker"):
            continue  # an enum nobody marked (a pin enum, a C-only enum) is read elsewhere or not at all
        members = parse_members(m.group("body"), f"{rel(path)}: {name}")
        alias = None
        if m.group("marker"):
            first = m.group("marker").split("\n")[0]
            _, tags = parse_tags(first.split(None, 1)[1] if " " in first else "", ENUM_MARK_TAGS, f"{rel(path)}: {name}")
            alias = tags.get("alias")
            if not alias:
                fail(f"{rel(path)}: {name}: `//#{m.group('kind')}` needs `@alias <the name a user sees>`")
        found[name] = {"alias": alias, "kind": m.group("kind"), "members": members, "source_file": rel(path)}
    return found


def enum_catalog():
    if "enums" not in _CACHE:
        catalog = {}
        for header in vm_headers():
            if "typedef enum" not in read(header):
                continue
            for name, enum in scan_enums(header).items():
                if enum["kind"] == "ref-enum":
                    if name in catalog:
                        fail(f"{rel(header)}: //#ref-enum {name} is declared twice (also in {catalog[name]['source_file']})")
                    catalog[name] = enum
        _CACHE["enums"] = catalog
    return _CACHE["enums"]


def published_enum(enum, name, context):
    """An enum as the json carries it: every member needs an alias, the count sentinels (`*_CNT`) are left out."""
    members = []
    for member in enum["members"]:
        if member["name"].endswith("_CNT"):
            continue
        if "alias" not in member:
            fail(f"{context}: enum {name}: {member['name']} needs `//@alias <what a user sees>`")
        members.append({k: v for k, v in member.items() if k in ("name", "value", "alias", "description")})
    return {"alias": enum["alias"], "source_file": enum["source_file"], "members": members}


# ---------------------------------------------------------------------------
# Errors
# ---------------------------------------------------------------------------

def error_tags():
    if "errors" not in _CACHE:
        modules = {}
        for path in COMPONENTS_DIR.rglob("err_modules.h"):
            modules.update({n: int(v) for n, v in MODULE_RE.findall(read(path))})
        tags = {}
        for header in COMPONENTS_DIR.rglob("*.h"):
            if "ERR_TAG(" not in read(header):
                continue
            for name, module, code in ERROR_RE.findall(read(header)):
                if module in modules:
                    tags[name] = (modules[module] << 16) | int(code)
        _CACHE["errors"] = tags
    return _CACHE["errors"]


# ---------------------------------------------------------------------------
# The block directive
# ---------------------------------------------------------------------------

def scan_directive(label, text):
    """`{symbol, tags: {keyword: value | [values]}}` from the `//#block VM_BLK_<NAME>` line and the `//@` lines under it, or None
    when the header has no such line (shared code). Any other line starting `//@` is an error: a tag stands on its element."""
    lines = text.split("\n")
    heads = [i for i, line in enumerate(lines) if BLOCK_START_RE.match(line)]
    if len(heads) > 1:
        fail(f"{label}: {len(heads)} `//#block` lines: one block per header")
    if not heads:
        for k, line in enumerate(lines):
            if line.startswith("//@"):
                fail(f"{label}:{k + 1}: a `//@` line without a `//#block VM_BLK_<NAME>` line above it")
        return None
    start = heads[0]
    tags, entries = {}, []
    i = start + 1
    while i < len(lines) and lines[i].startswith("//"):
        d = DIRECTIVE_RE.match(lines[i])
        if d:
            keyword = d.group("keyword")
            if keyword not in BLOCK_TAGS:
                fail(f"{label}:{i + 1}: unknown block tag //@{keyword} (known: {', '.join(sorted(BLOCK_TAGS))})")
            entries.append([keyword, (d.group("rest") or "").strip(), i + 1])
        elif lines[i].startswith("//#"):
            break
        else:
            if not entries:
                fail(f"{label}:{i + 1}: a comment line right under `//#block` before any //@ tag")
            entries[-1][1] = (entries[-1][1] + " " + lines[i][2:].strip()).strip()
        i += 1
    for keyword, value, line in entries:
        if BLOCK_TAGS[keyword] == "many":
            tags.setdefault(keyword, []).append({"text": value, "line": line})
        else:
            if keyword in tags:
                fail(f"{label}:{line}: //@{keyword} given twice")
            tags[keyword] = value
    # a `//@` line elsewhere (outside the directive) is a mistake
    directive_lines = {line for _, _, line in entries}
    for k, line in enumerate(lines):
        if line.startswith("//@") and k + 1 not in directive_lines:
            fail(f"{label}:{k + 1}: `//@` line outside the block directive (a tag on a field, pin or member stands at the end of its line)")
    symbol = BLOCK_START_RE.match(lines[start]).group(1)
    return {"symbol": symbol, "tags": tags}


def tag_one(tags, keyword, context, required=True):
    value = tags.get(keyword)
    if value is None:
        if required:
            fail(f"{context}: needs `//@{keyword} ...` in the block directive")
        return None
    if not value:
        fail(f"{context}: //@{keyword} is empty")
    return value


# ---------------------------------------------------------------------------
# State struct
# ---------------------------------------------------------------------------

def declared_types():
    if "vmtypes" not in _CACHE:
        found = {}
        for header in vm_headers():
            for name, size, align in VM_TYPE_RE.findall(read(header)):
                found[name] = (int(size), int(align))
        _CACHE["vmtypes"] = found
    return {**TYPES, **_CACHE["vmtypes"]}


def find_struct(name):
    """(body, align_attr, static_size, source path, header text) of `typedef struct ... { body } name;`."""
    pattern = re.compile(r"typedef\s+struct\s*(?P<attr>__attribute__\s*\(\(\s*aligned\((?P<align>\d+)\)\s*\)\))?\s*\w*\s*\{(?P<body>[^{}]*)\}\s*" + re.escape(name) + r"\s*;")
    for path in vm_headers():
        text = read(path)
        m = pattern.search(text)
        if m:
            size = re.search(r"_Static_assert\(\s*sizeof\(\s*" + re.escape(name) + r"\s*\)\s*==\s*(\d+)", text)
            return m.group("body"), int(m.group("align") or 1), int(size.group(1)) if size else None, path, text
    fail(f"state struct {name} not found under components/")


def struct_layout(name, context, enums=None):
    key = ("layout", name)
    if key in _CACHE:
        return _CACHE[key]
    body, attr_align, static_size, source, source_text = find_struct(name)
    if static_size is None:
        fail(f"{context}: {name} needs `_Static_assert(sizeof({name}) == N, ...)` so its layout is checked against the compiler")
    declarations = []  # (field line, its comment): a comment-only `//` line below a field continues that field's comment
    for line in body.splitlines():
        if not line.strip():
            continue
        if line.strip().startswith("//"):
            if declarations:
                declarations[-1][1].append(line.strip()[2:].strip())
            continue
        declarations.append((line, []))
    fields, offset, max_align = [], 0, attr_align
    types = declared_types()
    for line, continuation in declarations:
        m = FIELD_RE.match(line)
        if not m:
            fail(f"{context}: cannot parse state field `{line.strip()}` in {name}")
        c_type = " ".join(m.group("type").split())
        if c_type not in types:
            fail(f"{context}: state field type `{c_type}` has no known size (declare it `//#vm-type {c_type} @size <n> @align <n>` where it is defined)")
        size, align = types[c_type]
        offset = (offset + align - 1) // align * align
        max_align = max(max_align, align)
        field_ctx = f"{context}.{m.group('name')}"
        comment = " ".join([m.group("comment") or "", *continuation]).strip()
        description, tags = parse_tags(comment, FIELD_TAGS, field_ctx)
        if "description" in tags:
            if description:
                fail(f"{field_ctx}: text before the tags and @description: use one")
            description = tags.pop("description")
        field = {"name": m.group("name"), "c_type": c_type, "offset": offset, "_tags": tags}
        arr = m.group("arr")
        if arr is None:
            field["size"] = size
            offset += size
        elif arr == "":
            field["flexible"] = True
            field["element_size"] = size
        else:
            count = eval_c(arr, field_ctx)
            field.update(size=size * count, array_len=count, element_size=size)
            offset += size * count
        if description:
            field["description"] = description
        fields.append(field)
    total = (offset + max_align - 1) // max_align * max_align
    if static_size != total:
        fail(f"{context}: computed size of {name} is {total}, _Static_assert says {static_size}")
    by_name = {f["name"]: f for f in fields}
    for field_name, value in re.findall(r"_Static_assert\(\s*offsetof\(\s*" + re.escape(name) + r"\s*,\s*(\w+)\s*\)\s*==\s*(\d+)\s*,", source_text):
        if field_name not in by_name:
            fail(f"{context}: _Static_assert(offsetof({name}, {field_name})) names no field")
        if by_name[field_name]["offset"] != int(value):
            fail(f"{context}: {name}.{field_name} computes to offset {by_name[field_name]['offset']}, _Static_assert says {value}")
    layout = {"struct": name, "source_file": rel(source), "size": total, "align": max_align, "fields": fields}
    _CACHE[key] = layout
    return layout


def field_metadata(tags, context):
    """Editor metadata of a state field or a pin (`@id`, `@device-field`, ...), checked for shape."""
    metadata = {}
    if "id" in tags:
        if tags["id"] not in ID_KINDS:
            fail(f"{context}: @id must be one of {', '.join(ID_KINDS)}")
        metadata["id_kind"] = tags["id"]
    if "any" in tags:
        if not re.fullmatch(r"\d+", tags["any"]) or int(tags["any"]) > 0xFFFF:
            fail(f"{context}: @any needs the wildcard value (0 to 65535)")
        metadata["any_value"] = int(tags["any"])
    for tag in ("device-field", "contract", "device-type", "let-user-select-available", "source-field"):
        if tag in tags:
            if not IDENT_RE.match(tags[tag]):
                fail(f"{context}: @{tag} needs one identifier")
            metadata[tag.replace("-", "_")] = tags[tag]
    for tag in ("min", "max"):
        if tag in tags:
            if not re.fullmatch(r"-?\d+", tags[tag]):
                fail(f"{context}: @{tag} needs a whole number")
            metadata[tag] = int(tags[tag])
    if "unit" in tags:
        if not tags["unit"]:
            fail(f"{context}: @unit is empty")
        metadata["unit"] = tags["unit"]
    if "hidden-by-default" in tags:
        metadata["hidden_by_default"] = True
    if "dynamic-input" in tags:
        if not re.fullmatch(r"\d+", tags["dynamic-input"]):
            fail(f"{context}: @dynamic-input needs an input index")
        metadata["dynamic_input"] = int(tags["dynamic-input"])
    return metadata


def build_state(layout, context):
    """The state as the json carries it: fields with their source (user / runtime / derived / padding) and aliases."""
    fields = []
    for raw in layout["fields"]:
        tags = raw["_tags"]
        field_ctx = f"{context}.{raw['name']}"
        field = {k: v for k, v in raw.items() if k != "_tags"}
        if raw["name"].startswith("_"):
            field["source"] = "padding"
        elif "runtime" in tags:
            field["source"] = "runtime"
        elif "derived" in tags:
            if not tags["derived"]:
                fail(f"{field_ctx}: @derived needs the rule that computes it")
            field["source"] = "derived"
            field["derived"] = tags["derived"]
        else:
            field["source"] = "user"
        if "alias" in tags:
            if not tags["alias"]:
                fail(f"{field_ctx}: @alias is empty")
            field["alias"] = tags["alias"]
        if "role" in tags:
            if tags["role"] not in FIELD_ROLES:
                fail(f"{field_ctx}: @role must be one of {', '.join(FIELD_ROLES)}")
            field["role"] = tags["role"]
        field.update(field_metadata(tags, field_ctx))
        if "enum-ref" in tags:
            field["enum_ref"] = tags["enum-ref"]
        fields.append(field)
    return {"struct": layout["struct"], "source_file": layout["source_file"], "size": layout["size"], "align": layout["align"], "fields": fields}


# ---------------------------------------------------------------------------
# Pins
# ---------------------------------------------------------------------------

def pins_of(text, block, context, header_path):
    """The pin entries from the members of `vm_in_<block>_e` / `vm_out_<block>_e`: `VM_IN_<BLOCK>_<PIN> = <index>,
    //@... @alias <name> @value <kind> ...`; a repeating pin is the last member `..._<PIN>_ANY` with `@repeat`."""
    entries = []
    for side, prefix in (("in", "VM_IN_"), ("out", "VM_OUT_")):
        enum_name = f"vm_{side}_{block.lower()}_e"
        m = next((e for e in ENUM_RE.finditer(text) if e.group("name") == enum_name), None)
        if not m:
            continue
        values = {mem["name"]: mem["value"] for mem in parse_members(re.sub(r"//[^\n]*", "", m.group("body")), f"{context}: {enum_name}")}
        for raw in m.group("body").split("\n"):
            line = raw.strip()
            if not line or line.startswith("//"):
                continue
            mm = MEMBER_RE.match(line)
            if not mm:
                fail(f"{context}: {enum_name}: cannot parse `{line}`")
            const = mm.group("name")
            where = f"{context}: {const}"
            if not const.startswith(f"{prefix}{block}_"):
                fail(f"{where}: must be named {prefix}{block}_<PIN>")
            lead, tags = parse_tags(mm.group("comment") or "", PIN_TAGS, where)
            if lead:
                fail(f"{where}: unexpected text `{lead}` before the first tag")
            name = const[len(prefix) + len(block) + 1:].lower()
            index = values[const]
            if "repeat" in tags:
                if not name.endswith("_any"):
                    fail(f"{where}: a @repeat pin member is named {prefix}{block}_<PIN>_ANY")
                name, index = name[:-len("_any")], "*"
            entries.append({"side": side, "index": index, "name": name, "tags": tags, "where": where, "value": values[const]})
    for side in ("in", "out"):
        mine = [e for e in entries if e["side"] == side]
        numbered = [e for e in mine if e["index"] != "*"]
        for e in mine:
            if e["index"] == "*" and e["value"] != len(numbered):
                fail(f"{context}: the repeating pin `{e['name']}` is valued {e['value']}, after {len(numbered)} numbered pin(s) it must be {len(numbered)}")
    return entries


def build_pins(entries, context):
    """(inputs, outputs): pin dicts with alias, description, value kind and editor metadata."""
    pins = {"in": [], "out": []}
    for e in entries:
        where, tags = e["where"], e["tags"]
        if not tags.get("alias"):
            fail(f"{where}: needs `@alias <the pin's name a user sees>`")
        if tags.get("value") not in VALUE_KINDS:
            fail(f"{where}: needs @value, one of {sorted(VALUE_KINDS)}")
        pin = {"index": e["index"], "name": e["name"], "alias": tags["alias"], "value": tags["value"]}
        if "description" in tags:
            if not tags["description"]:
                fail(f"{where}: @description is empty")
            pin["description"] = tags["description"]
        pin.update(field_metadata({k: v for k, v in tags.items() if k in ("id", "device-field", "unit")}, where))
        if "spec" in tags:
            if not IDENT_RE.match(tags["spec"]) or e["side"] != "in":
                fail(f"{where}: @spec names one device value (`//#value NAME`) and belongs on an input pin")
            pin["value_spec"] = tags["spec"]
        if "user-write" in tags:
            if e["side"] != "in":
                fail(f"{where}: @user-write belongs on an input pin")
            pin["user_write"] = True
        if "hide-when-wired" in tags:
            if e["side"] != "in":
                fail(f"{where}: @hide-when-wired belongs on an input pin")
            pin["hide_when_wired"] = True
        if "overrides" in tags:
            if not IDENT_RE.match(tags["overrides"]):
                fail(f"{where}: @overrides needs one state field name")
            pin["overrides"] = tags["overrides"]
        pin["required"] = "required" in tags and e["index"] != "*"
        pin["_required"] = "required" in tags
        pin["_where"] = where
        pins[e["side"]].append(pin)
    names = [p["name"] for side in pins.values() for p in side]
    for name in sorted({n for n in names if names.count(n) > 1}):
        fail(f"{context}: pin name `{name}` is used twice (pin names are unique across the block so {{{name}}} is unambiguous)")
    for side in ("in", "out"):
        stars = [p for p in pins[side] if p["index"] == "*"]
        if len(stars) > 1:
            fail(f"{context}: at most one repeating {side} pin")
        if stars and pins[side][-1] is not stars[0]:
            fail(f"{context}: the repeating {side} pin must be the last member")
    return pins["in"], pins["out"]


def pin_shape(pins, limit_symbol):
    """The (min, max) pin counts the pins imply, and for inputs the required-pin mask."""
    required_numbered = [p["index"] for p in pins if p["index"] != "*" and p["_required"]]
    star = next((p for p in pins if p["index"] == "*"), None)
    numbered = sum(1 for p in pins if p["index"] != "*")
    minimum = max(required_numbered) + 1 if required_numbered else 0
    if star and star["_required"]:
        minimum = max(minimum, numbered + 1)
    maximum = limit_symbol if star else numbered
    return minimum, maximum


def link_overrides(inputs, fields, context):
    """Tie each `@overrides` pin to its state field: the constant used while the pin is unwired."""
    by_name = {f["name"]: f for f in fields}
    for pin in inputs:
        target = pin.get("overrides")
        if not target:
            continue
        where = pin["_where"]
        field = by_name.get(target)
        if not field or field["source"] != "user" or field.get("flexible"):
            fail(f"{where}: @overrides {target} must name a user state field of the block")
        if field["c_type"] not in OVERRIDE_TYPES.get(pin["value"], set()):
            fail(f"{where}: @overrides {target} is a {field['c_type']}, which a {pin['value']} pin cannot replace")
        if "overridden_by" in field:
            fail(f"{where}: {target} is already overridden by another pin")
        if pin["index"] == "*":
            fail(f"{where}: a `*` pin cannot override one field")
        if pin["required"]:
            fail(f"{where}: a required input cannot have a constant to fall back on")
        field["overridden_by"] = pin["index"]


def check_aliases(fields, enums, context):
    """Every setting a user can edit has a name; an enum a setting uses publishes every member with its alias."""
    for field in fields:
        if field["source"] != "user":
            continue
        if "alias" not in field and "overridden_by" not in field and field.get("role") not in ("const", "idle-const"):
            fail(f"{context}.{field['name']}: a setting the user can edit needs `@alias <its name>` (or @runtime / @derived if the app doesn't write it)")
        if "enum_ref" in field and field["enum_ref"] not in enums:
            fail(f"{context}.{field['name']}: @enum-ref {field['enum_ref']} is neither a //#block-enum of the block nor a //#ref-enum")


def validate_editor_links(block, context):
    fields = {field["name"]: field for field in block.get("state", {}).get("fields", [])}
    pins = block["inputs"]["pins"]
    for entry in [*fields.values(), *pins, *block["outputs"]["pins"]]:
        device = entry.get("device_field")
        if device and fields.get(device, {}).get("id_kind") != "device":
            fail(f"{context}.{entry['name']}: @device-field {device} must name a device ID field")
        kind = entry.get("id_kind")
        if kind == "pin" and not device:
            fail(f"{context}.{entry['name']}: a pin ID needs @device-field")
        if kind in ("channel", "event") and not device:
            fail(f"{context}.{entry['name']}: a {kind} ID needs @device-field")
        if kind == "device" and not (entry.get("contract") or entry.get("device_type") or "any_value" in entry):
            fail(f"{context}.{entry['name']}: a device ID needs @contract <name>, @device-type <name> or @any (events: any device)")
        source = entry.get("source_field")
        if source and not fields.get(source, {}).get("enum_ref"):
            fail(f"{context}.{entry['name']}: @source-field {source} must name an enum field")
        selection = entry.get("let_user_select_available")
        if selection:
            if fields.get(selection, {}).get("id_kind") != "pin" or entry.get("c_type") != "uint64_t":
                fail(f"{context}.{entry['name']}: a selectable mask must be uint64_t and name a pin ID field")
            if not any(pin["index"] == entry.get("dynamic_input") and not pin.get("required") for pin in pins):
                fail(f"{context}.{entry['name']}: @dynamic-input must name an optional input")


# ---------------------------------------------------------------------------
# Face: templates with {ref}
# ---------------------------------------------------------------------------

def resolve_ref(name, scope, context):
    if name == "title":
        return {"ref": name, "kind": "title"}
    if name in scope.get("extra", {}):
        return {"ref": name, "kind": scope["extra"][name]}
    pin = scope["inputs"].get(name)
    field = scope["fields"].get(name)
    if pin is not None:
        if field is not None and pin.get("overrides") != name:
            fail(f"{context}: {{{name}}} is both an input pin and a state field; add `@overrides {name}` to the pin or rename one")
        if pin["index"] == "*":
            fail(f"{context}: {{{name}}} is a repeating pin and cannot be referenced")
        part = {"ref": name, "kind": "pin", "pin": pin["index"]}
        if pin.get("overrides"):
            part["field"] = pin["overrides"]
        return part
    if name in scope["outputs"]:
        pin = scope["outputs"][name]
        if pin["index"] == "*":
            fail(f"{context}: {{{name}}} is a repeating pin and cannot be referenced")
        return {"ref": name, "kind": "out", "pin": pin["index"]}
    if field is not None:
        if field["source"] != "user" or field.get("flexible"):
            fail(f"{context}: {{{name}}} is not a setting the user enters (state field source {field['source']})")
        return {"ref": name, "kind": "field", "field": name}
    known = sorted({"title", *scope.get("extra", {}), *scope["inputs"], *scope["outputs"], *(n for n, f in scope["fields"].items() if f["source"] == "user")})
    fail(f"{context}: {{{name}}} names no pin or setting (known: {', '.join(known)})")


def parse_template(text, scope, context):
    """A line of text with `{ref}`s as parts: `{"text": ..}` and `{"ref": ..., "kind": ...}`."""
    parts, pos = [], 0
    for m in REF_RE.finditer(text):
        if m.start() > pos:
            parts.append({"text": text[pos:m.start()]})
        parts.append(resolve_ref(m.group(1), scope, context))
        pos = m.end()
    if pos < len(text):
        parts.append({"text": text[pos:]})
    for part in parts:
        if "text" in part and ("{" in part["text"] or "}" in part["text"]):
            fail(f"{context}: stray brace in `{text}` (a reference is {{name}} with a known name)")
    if parts and "text" in parts[0]:
        parts[0]["text"] = parts[0]["text"].lstrip()
    if parts and "text" in parts[-1]:
        parts[-1]["text"] = parts[-1]["text"].rstrip()
    parts = [p for p in parts if "ref" in p or p["text"]]
    if not parts:
        fail(f"{context}: empty template")
    return parts


def build_face(text, scope, context):
    """The block face's line under the title: `<lead> | <value>` (the `|` is optional), or None."""
    if not text:
        return None
    lead, _, value = text.partition("|")
    face = {"lead": parse_template(lead, scope, f"{context}: face")}
    if value.strip():
        face["value"] = parse_template(value, scope, f"{context}: face")
    return face


# ---------------------------------------------------------------------------
# Bytecode encoding (EXPR / EXPR_BIT)
# ---------------------------------------------------------------------------

STACK_LETTERS = "abcd"


def opcode_table(enum_name, members, context):
    entries = {}
    for header in vm_headers():
        for m in OPENTRY_RE.finditer(read(header)):
            entries.setdefault(m.group("sym"), m)
    ops = []
    for member in members:
        if member["name"].endswith("_CNT"):
            continue
        e = entries.get(member["name"])
        if not e:
            fail(f"{context}: opcode {member['name']} has no `[{member['name']}] = {{pops, pushes, VM_*ARG_*}}` entry in the opcode table")
        pops, pushes, arg = int(e.group("pops")), int(e.group("pushes")), e.group("arg").lower()
        op = {"symbol": member["name"], "value": member["value"], "alias": member["alias"], "pops": pops, "pushes": pushes, "operand": arg}
        if "description" in member:
            op["description"] = member["description"]
        fmt = member.get("format")
        if fmt is None and not member.get("stack_op") and member["value"] != 0:
            fail(f"{context}: opcode {member['name']} needs `@format <how it reads in a formula>` (`{{a}} + {{b}}`, `sqrt({{a}})`) or `@stack-op`")
        if fmt is not None:
            used = set(REF_RE.findall(fmt))
            allowed = set(STACK_LETTERS[:pops]) | ({"input"} if arg == "input" else set()) | ({"const"} if arg == "const" else set())
            if not used <= allowed:
                fail(f"{context}: opcode {member['name']}: @format `{fmt}` uses {sorted(used - allowed)}; it can use {sorted(allowed)}")
            if pushes != 1:
                fail(f"{context}: opcode {member['name']}: @format is for an opcode that leaves one value (it pushes {pushes}); mark it @stack-op")
            if arg == "none" and set(STACK_LETTERS[:pops]) - used:
                fail(f"{context}: opcode {member['name']}: @format `{fmt}` does not use every operand ({', '.join('{' + c + '}' for c in STACK_LETTERS[:pops])})")
            op["format"] = fmt
        elif member.get("stack_op"):
            op["stack_op"] = True
        ops.append(op)
    extra = set(entries) & set(m["name"] for m in members)
    return ops


def check_code(code, ops, in_cnt, const_cnt, stack_max):
    """The block's load-time bytecode check, for the examples."""
    by_value = {o["value"]: o for o in ops}
    sp, pc = 0, 0
    while pc < len(code):
        op = by_value.get(code[pc])
        if op is None:
            return f"unknown opcode {code[pc]} at {pc}"
        pc += 1
        if op["value"] == 0:
            break
        if op["operand"] != "none":
            if pc >= len(code):
                return f"{op['symbol']} at {pc - 1} has no operand"
            arg = code[pc]
            pc += 1
            if arg >= (in_cnt if op["operand"] == "input" else const_cnt):
                return f"{op['symbol']} operand {arg} out of range"
        if sp < op["pops"]:
            return f"{op['symbol']} underflows the stack"
        sp = sp - op["pops"] + op["pushes"]
        if sp > stack_max:
            return f"{op['symbol']} overflows the stack"
    return None if sp == 1 else f"stack ends with {sp} values"


def number_list(text):
    return [t.strip() for t in text.split(",") if t.strip()]


def constant(value, const_type, what):
    try:
        return float(value) if const_type == "f32" else int(value, 0)
    except ValueError:
        fail(f"{what}: `{value}` is not a {'number' if const_type == 'f32' else 'whole number'}")


def build_examples(examples, ops, const_type, stack_max, state, context):
    """Golden custom_data for each //@example: the state header is written into the computed layout, then constants, then code."""
    fields = {f["name"]: f for f in state["fields"]}
    for needed, width in (("const_cnt", 1), ("code_len", 2)):
        if needed not in fields or fields[needed].get("size") != width:
            fail(f"{context}: a bytecode block's state struct needs a {width}-byte `{needed}` field")
    symbols = {o["symbol"]: o for o in ops}
    out = []
    for entry in examples:
        where = f"{context}: //@example (line {entry['line']})"
        title, tags = parse_tags(entry["text"], EXAMPLE_TAGS, where)
        if not title or "result" not in tags or "code" not in tags:
            fail(f"{where}: needs a title, @code and @result")
        inputs = [constant(v, const_type, where) for v in number_list(tags.get("in", ""))]
        consts = [constant(v, const_type, where) for v in number_list(tags.get("consts", ""))]
        code, tokens, i = [], tags["code"].split(), 0
        while i < len(tokens):
            op = symbols.get(tokens[i])
            if not op:
                fail(f"{where}: unknown opcode {tokens[i]}")
            code.append(op["value"])
            i += 1
            if op["operand"] != "none":
                if i >= len(tokens):
                    fail(f"{where}: {op['symbol']} needs an operand")
                operand = constant(tokens[i], "u32", where)
                if not 0 <= operand < 256:
                    fail(f"{where}: operand {operand} does not fit a byte")
                code.append(operand)
                i += 1
        problem = check_code(code, ops, len(inputs), len(consts), stack_max)
        if problem:
            fail(f"{where}: `{title}` would be refused at load: {problem}")
        packed = b"".join(struct.pack("<f", c) if const_type == "f32" else struct.pack("<I", c) for c in consts)
        head = bytearray(state["size"])
        head[fields["const_cnt"]["offset"]] = len(consts)
        head[fields["code_len"]["offset"]:fields["code_len"]["offset"] + 2] = struct.pack("<H", len(code))
        custom = bytes(head) + packed + bytes(code)
        out.append({"title": title, "inputs": inputs, "constants": consts, "code": tokens, "custom_data": custom.hex(),
                    "custom_len": len(custom), "result": constant(tags["result"], const_type, where)})
    return out


def bytecode_encoding(tags, inputs, state, enums, scope, context):
    parts, rest = tags["opcodes"], None
    lead, opts = parse_tags(parts, {"stack-max", "const-count", "code-length"}, f"{context}: //@opcodes")
    if not IDENT_RE.match(lead) or {"stack-max", "const-count", "code-length"} - set(opts):
        fail(f"{context}: //@opcodes takes `<enum> @stack-max <macro> @const-count <field> @code-length <field>`")
    enum_name = lead
    if enum_name not in enums or enums[enum_name]["kind"] != "ref-enum":
        fail(f"{context}: //@opcodes {enum_name}: not a //#ref-enum")
    const_field, code_field = opts["const-count"], opts["code-length"]
    names = {f["name"] for f in state["fields"]}
    for field in (const_field, code_field):
        if field not in names:
            fail(f"{context}: //@opcodes names the state field `{field}`, which does not exist")
    ops = opcode_table(enum_name, enums[enum_name]["members"], context)
    stack_max = symbol_value(opts["stack-max"], context)
    star = next((p for p in inputs if p["index"] == "*"), None)
    const_type = star["value"] if star else "f32"
    if const_type not in ("f32", "u32"):
        fail(f"{context}: a bytecode block's repeating input must be f32 or u32 (its constants take that type), not {const_type}")
    encoding = {
        "kind": "bytecode",
        "layout": f"state header, then {const_field} constants of 4 bytes, then {code_field} code bytes; custom_len = {state['size']} + 4 x {const_field} + {code_field}",
        "const_count_field": const_field, "code_length_field": code_field, "constant_type": const_type,
        "stack_max": stack_max, "opcode_enum": enum_name, "opcodes": ops,
        "examples": build_examples(tags.get("example", []), ops, const_type, stack_max, state, context),
    }
    if "formula" in tags:
        encoding["formula"] = parse_template(tags["formula"], {**scope, "extra": {"expr": "expression"}}, f"{context}: formula")
    return encoding


# ---------------------------------------------------------------------------
# Device operations (a block over the shared device engine)
# ---------------------------------------------------------------------------

def op_values():
    """{op symbol: u16 op id}: the contracts' op tables (through their `X(NAME, n, args)` X-macros) and the `SYS_DEVICE_OWN_OP(n)` defines."""
    if "ops" in _CACHE:
        return _CACHE["ops"]
    values = {}
    contract_ids = {name: safe_int(value, name) for name, value in defines().items() if name.startswith("SYS_DEVICE_CONTRACT_")}
    for header in vm_headers():
        text = read(header)
        for name, n in re.findall(r"^#define\s+(\w+)\s+SYS_DEVICE_OWN_OP\(\s*(0x[0-9a-fA-F]+|\d+)\s*\)", text, re.MULTILINE):
            values[name] = (contract_ids["SYS_DEVICE_CONTRACT_OWN"] << 8) | int(n, 0)
        for id_macro, prefix, contract in re.findall(r"^#define\s+(\w+)\(\s*name\s*,\s*n\s*,\s*args_t\s*\)\s+(\w+)_##name\s*=\s*SYS_DEVICE_OP\(\s*(\w+)\s*,\s*n\s*\)", text, re.MULTILINE):
            table = re.search(r"enum\s*\{[^}]*?(\w+)\(\s*" + re.escape(id_macro) + r"\s*\)", text)
            if not table:
                continue
            body = re.search(r"^#define\s+" + re.escape(table.group(1)) + r"\(X\)(?P<body>(?:[^\n]*\\\n)*[^\n]*)", text, re.MULTILINE)
            if not body:
                continue
            for entry, n in re.findall(r"X\(\s*(\w+)\s*,\s*(0x[0-9a-fA-F]+|\d+)\s*,", body.group("body")):
                values[f"{prefix}_{entry}"] = (contract_ids[contract] << 8) | int(n, 0)
    _CACHE["ops"] = values
    return values


def balanced(text):
    depth = 0
    for ch in text:
        depth += ch in "({"
        depth -= ch in ")}"
    return depth == 0


def parse_op_row(row, context):
    """The op symbol, kind, result, field types and scales of one row of a `vm_dev_op_t` table (`VM_DEV_OP(...)` or a designated `{...}`)."""
    row = row.strip().rstrip(",").strip()
    if row.startswith("VM_DEV_OP("):
        args = split_top_level(row[len("VM_DEV_OP("):-1])
        if len(args) < 4:
            fail(f"{context}: VM_DEV_OP needs (op, act, result, n, field types...)")
        values = {"op": args[0], "act": args[1], "result": args[2], "nfield": args[3], "ftype": args[4:], "scale": []}
    elif row.startswith("{"):
        values = {"op": None, "act": None, "result": None, "nfield": "0", "ftype": [], "scale": []}
        for item in split_top_level(row[1:-1]):
            m = re.match(r"^\.(\w+)\s*=\s*(.*)$", item, re.DOTALL)
            if not m:
                fail(f"{context}: cannot read `{item}` in an op row")
            key, value = m.group(1), m.group(2).strip()
            if key in ("ftype", "scale"):
                value = split_top_level(value.strip()[1:-1])
            if key not in values:
                fail(f"{context}: unknown member .{key} in an op row")
            values[key] = value
    else:
        fail(f"{context}: an op row is `VM_DEV_OP(...)` or a designated `{{.op = ...}}`; got `{row[:50]}`")
    if values["op"] is None or values["act"] is None or values["result"] is None:
        fail(f"{context}: an op row needs .op, .act and .result")
    act = DEV_ACTS.get(values["act"].replace("VM_DEV_ACT_", ""))
    result = DEV_RESULTS.get(values["result"].replace("VM_DEV_R_", ""))
    if act is None or result is None:
        fail(f"{context}: unknown act `{values['act']}` or result `{values['result']}`")
    nfield = int(values["nfield"])
    ftypes = []
    for t in values["ftype"]:
        t = t.replace("VM_DEV_F_", "")
        if t not in DEV_FIELD_TYPES:
            fail(f"{context}: unknown field type `{t}`")
        ftypes.append(t)
    if len(ftypes) != nfield:
        fail(f"{context}: {values['op']} has nfield {nfield} but {len(ftypes)} field types")
    scales = [float(s.rstrip("fF")) for s in values["scale"]] + [0.0] * (nfield - len(values["scale"]))
    return {"symbol": values["op"], "act": act, "result": values["result"].replace("VM_DEV_R_", ""), "ftypes": ftypes, "scales": scales[:nfield]}


def parse_op_table(c_text, context):
    """The rows of `static const vm_dev_op_t k_ops[] = {...}` with the `//@op` / `//@op-in` / `//@op-out` lines above each."""
    m = re.search(r"static\s+const\s+vm_dev_op_t\s+k_ops\[\]\s*=\s*\{", c_text)
    if not m:
        fail(f"{context}: a device block's .c has `static const vm_dev_op_t k_ops[] = {{...}};`")
    depth, end = 1, m.end()
    while depth and end < len(c_text):
        depth += (c_text[end] == "{") - (c_text[end] == "}")
        end += 1
    body = c_text[m.end():end - 1]
    rows, pending, buf = [], [], ""
    for line in body.split("\n"):
        stripped = line.strip()
        if not buf and stripped.startswith("//@op"):
            pending.append(stripped)
            continue
        if not buf and (not stripped or stripped.startswith("//")):
            continue
        buf += " " + re.sub(r"//.*$", "", line)
        if balanced(buf) and buf.rstrip().endswith(","):
            rows.append((buf.strip(), pending))
            buf, pending = "", []
    if buf.strip():
        rows.append((buf.strip(), pending))
    return rows


def parse_choices(text, context):
    choices = []
    for item in text.replace(",", " ").split():
        m = re.fullmatch(r"(-?\d+)=(.+)", item)
        if not m:
            fail(f"{context}: @choices is `<value>=<Name>` items separated by spaces or commas (use _ for a space inside a name); got `{item}`")
        choices.append({"value": int(m.group(1)), "alias": m.group(2).replace("_", " ")})
    return choices


def build_ops(c_text, device, block_id_symbol, context):
    ids = op_values()
    ops = []
    for row, pending in parse_op_table(c_text, context):
        spec = parse_op_row(row, context)
        symbol = spec["symbol"]
        where = f"{context}: op {symbol}"
        if symbol not in ids:
            fail(f"{where}: no op id found (a contract op is `X(NAME, n, args)` in its table; a device's own op is `#define {symbol} SYS_DEVICE_OWN_OP(n)`)")
        head = [p for p in pending if re.match(r"^//@op\s", p)]
        ins = [p for p in pending if p.startswith("//@op-in")]
        outs = [p for p in pending if p.startswith("//@op-out")]
        if len(head) != 1:
            fail(f"{where}: needs one `//@op {symbol} @alias <name> @description <text>` line above its row")
        lead, tags = parse_tags(head[0][len("//@op"):], OP_TAGS, where)
        if lead != symbol:
            fail(f"{where}: the `//@op` line above names `{lead}`, the row is {symbol}")
        if not tags.get("alias") or not tags.get("description"):
            fail(f"{where}: needs @alias and @description")
        out_type, out_count, out_bytes = DEV_RESULTS[spec["result"]]
        if len(ins) != len(spec["ftypes"]):
            fail(f"{where}: the row has {len(spec['ftypes'])} input field(s), {len(ins)} `//@op-in` line(s)")
        if len(outs) != out_count:
            fail(f"{where}: the row answers {out_count} value(s), {len(outs)} `//@op-out` line(s)")
        op = {"symbol": symbol, "value": ids[symbol], "alias": tags["alias"], "description": tags["description"], "kind": spec["act"],
              "idle_allowed": spec["act"] != "read", "inputs": [], "outputs": []}
        for index, (line, ftype) in enumerate(zip(ins, spec["ftypes"])):
            lead, t = parse_tags(line[len("//@op-in"):], OP_PIN_TAGS, f"{where} input {index}")
            if not IDENT_RE.match(lead) or not t.get("alias"):
                fail(f"{where} input {index}: `//@op-in <name> @alias <what a user sees> ...`")
            pin = {"index": index, "name": lead, "alias": t["alias"], "field_type": DEV_FIELD_TYPES[ftype][0]}
            if spec["scales"][index]:
                pin["scale"] = spec["scales"][index]
            if "description" in t:
                pin["description"] = t["description"]
            pin.update(field_metadata({k: v for k, v in t.items() if k in ("id", "unit", "min", "max")}, f"{where} input {index}"))
            if "choices" in t:
                pin["choices"] = parse_choices(t["choices"], f"{where} input {index}")
            if pin.get("id_kind") in ("pin", "channel", "event"):
                pin["device_field"] = "device_id"
            op["inputs"].append(pin)
        for index, line in enumerate(outs):
            lead, t = parse_tags(line[len("//@op-out"):], OP_PIN_TAGS, f"{where} output {index}")
            if not IDENT_RE.match(lead) or not t.get("alias"):
                fail(f"{where} output {index}: `//@op-out <name> @alias <what a user sees> ...`")
            pin = {"index": index, "name": lead, "alias": t["alias"], "value_type": out_type}
            if "description" in t:
                pin["description"] = t["description"]
            pin.update(field_metadata({k: v for k, v in t.items() if k in ("unit",)}, f"{where} output {index}"))
            if "choices" in t:
                pin["choices"] = parse_choices(t["choices"], f"{where} output {index}")
            op["outputs"].append(pin)
        names = [p["name"] for p in op["inputs"] + op["outputs"]]
        for name in sorted({n for n in names if names.count(n) > 1}):
            fail(f"{where}: pin name `{name}` is used twice")
        scope = {"inputs": {p["name"]: {"index": p["index"]} for p in op["inputs"]},
                 "outputs": {p["name"]: {"index": p["index"]} for p in op["outputs"]}, "fields": {}, "extra": {"op": "op", "device": "device"}}
        if "face" in tags:
            op["face"] = build_face(tags["face"], scope, where)
        ops.append(op)
    symbols = [o["symbol"] for o in ops]
    for dup in sorted({s for s in symbols if symbols.count(s) > 1}):
        fail(f"{context}: op {dup} is in the table twice")
    return ops


# ---------------------------------------------------------------------------
# One block
# ---------------------------------------------------------------------------

def parse_block_c(c_text, symbol, context):
    """The arguments of `VM_BLOCK_TYPE_DEFINE(<symbol>, ...)`: {run, check, min_in, ..., state_len} as raw expressions."""
    m = re.search(r"VM_BLOCK_TYPE_DEFINE\(\s*" + re.escape(symbol) + r"\s*,", c_text)
    if not m:
        fail(f"{context}: no `VM_BLOCK_TYPE_DEFINE({symbol}, ...)` in the .c file")
    depth, end = 1, m.end()
    while depth and end < len(c_text):
        depth += (c_text[end] == "(") - (c_text[end] == ")")
        end += 1
    args = {}
    for item in split_top_level(re.sub(r"//[^\n]*", "", c_text[m.end():end - 1])):
        mm = re.match(r"^\.(\w+)\s*=\s*(.*)$", item, re.DOTALL)
        if not mm:
            fail(f"{context}: cannot read `{item}` in VM_BLOCK_TYPE_DEFINE")
        args[mm.group(1)] = " ".join(mm.group(2).split())
    return args


def block_ids():
    if "block_ids" not in _CACHE:
        found = {}
        for name, value in re.findall(r"^#define\s+(VM_BLK_\w+)\s+(\d+)\b", read(IDS_HEADER), re.MULTILINE):
            found[name] = int(value)
        _CACHE["block_ids"] = found
    return _CACHE["block_ids"]


def build_block(path, text, directive, enums_all):
    label = rel(path)
    context = label
    tags = directive["tags"]
    symbol = directive["symbol"]
    name = symbol[len("VM_BLK_"):]
    block_id = block_ids().get(symbol)
    if block_id is None:
        fail(f"{context}: {symbol} has no `#define {symbol} <id>` in {rel(IDS_HEADER)}")
    for key in REQUIRED_BLOCK_TAGS:
        tag_one(tags, key, context)
    if tags["activation"] not in ACTIVATION_KINDS:
        fail(f"{context}: //@activation must be one of {', '.join(ACTIVATION_KINDS)}")
    enables = tags.get("enables", "optional")
    if enables not in ("required", "optional"):
        fail(f"{context}: //@enables is `required` or `optional`")
    loop = tags.get("loop", "none")
    if loop not in ("none", *LOOP_USES):
        fail(f"{context}: //@loop is `refuse`, `once` or `none`")
    c_path = path.with_suffix(".c")
    if not c_path.exists():
        fail(f"{context}: {rel(c_path)} not found (the body and VM_BLOCK_TYPE_DEFINE)")
    c_text = read(c_path)
    shape_c = parse_block_c(c_text, symbol, rel(c_path))

    # enums the block owns (//#block-enum) plus the shared ones it references
    own_enums = {n: e for n, e in scan_enums(path).items() if e["kind"] == "block-enum"}
    enums = {**enums_all, **own_enums}

    device = None
    if "contract" in tags or "device-type" in tags:
        if "contract" in tags and "device-type" in tags:
            fail(f"{context}: //@contract and //@device-type are exclusive")
        if "contract" in tags:
            cid = defines().get(f"SYS_DEVICE_CONTRACT_{tags['contract']}")
            if cid is None:
                fail(f"{context}: //@contract {tags['contract']}: no SYS_DEVICE_CONTRACT_{tags['contract']} define")
            device = {"contract": tags["contract"], "contract_id": safe_int(cid, context)}
        else:
            device = {"device_type": tags["device-type"]}

    entries = pins_of(text, name, context, path)
    inputs, outputs = build_pins(entries, context)
    if device and entries:
        fail(f"{context}: a device block takes its pins from the ops table, not from vm_in_/vm_out_ enums")

    state = None
    if "data" in tags:
        layout = struct_layout(tags["data"], context)
        state = build_state(layout, context)
        link_overrides(inputs, state["fields"], context)
        check_aliases(state["fields"], enums, context)
    elif inputs and any("overrides" in p for p in inputs):
        fail(f"{context}: a pin with @overrides needs //@data <struct>")

    # shape the firmware enforces, from VM_BLOCK_TYPE_DEFINE, checked against the pins
    shape = {}
    for key in ("min_in", "max_in", "min_q", "max_q", "min_en", "required_in", "state_len"):
        shape[key] = eval_c(shape_c[key], f"{rel(c_path)}: .{key}") if key in shape_c else 0
    if "run" not in shape_c or shape_c["run"] != f"vm_blk_{name.lower()}":
        fail(f"{rel(c_path)}: .run must be vm_blk_{name.lower()} (got {shape_c.get('run')})")
    if f"void vm_blk_{name.lower()}(vm_block_h b);" not in text:
        fail(f"{context}: declare the body `void vm_blk_{name.lower()}(vm_block_h b);`")
    check_name = shape_c.get("check")
    if check_name is not None and f"bool {check_name}(vm_block_h b);" not in text:
        fail(f"{context}: declare the check `bool {check_name}(vm_block_h b);`")
    state_size = state["size"] if state else 0
    if shape["state_len"] != state_size and not (state is None and shape["state_len"] == 0):
        fail(f"{rel(c_path)}: .state_len is {shape['state_len']}, the state {tags.get('data')} is {state_size} bytes")
    if (1 if enables == "required" else 0) != shape["min_en"]:
        fail(f"{rel(c_path)}: .min_en is {shape['min_en']}, //@enables is `{enables}`")

    ops = None
    if device:
        ops = build_ops(c_text, device, symbol, rel(c_path))
        fan_in = max((len(o["inputs"]) for o in ops), default=0)
        fan_out = max((len(o["outputs"]) for o in ops), default=0)
        if shape["max_in"] < fan_in or shape["max_q"] < fan_out:
            fail(f"{rel(c_path)}: the ops need up to {fan_in} inputs / {fan_out} outputs, VM_BLOCK_TYPE_DEFINE allows {shape['max_in']} / {shape['max_q']}")
        if shape["min_in"] or shape["min_q"] or shape["required_in"]:
            fail(f"{rel(c_path)}: a device block has no required pins (min_in / min_q / required_in are 0)")
        if state is None:
            fail(f"{context}: a device block needs //@data <struct> (the device engine's state)")
        roles = {f.get("role"): f for f in state["fields"] if f.get("role")}
        for needed in ("device", "op", "idle-op", "const", "idle-const"):
            if needed not in roles:
                fail(f"{context}: the device state {state['struct']} needs a field with @role {needed}")
        device_field = roles["device"]
        if device_field.get("id_kind") != "device":
            fail(f"{context}: the @role device field must be `@id device`")
        # the engine's state is shared: which devices fit is the block's (//@contract / //@device-type)
        device_field.pop("contract", None)
        device_field.pop("device_type", None)
        if "contract" in device:
            device_field["contract"] = device["contract"]
        else:
            device_field["device_type"] = device["device_type"]
    else:
        min_in, max_in = pin_shape(inputs, "CONFIG_VM_BLOCK_MAX_IN")
        min_q, max_q = pin_shape(outputs, "CONFIG_VM_BLOCK_MAX_OUT")
        required = sum(1 << p["index"] for p in inputs if p["index"] != "*" and p["_required"])
        expect = {"min_in": min_in, "max_in": max_in, "min_q": min_q, "max_q": max_q, "required_in": required}
        for key, want in expect.items():
            want_value = eval_c(want, context) if isinstance(want, str) else want
            if shape[key] != want_value:
                fail(f"{rel(c_path)}: .{key} is {shape[key]}, the pins imply {want_value} ({key.split('_')[0]} pins and @required in the header)")

    # JSON assembly
    def group(pins, limit_symbol, minimum, maximum):
        limit = sdkconfig()[limit_symbol]
        star = any(p["index"] == "*" for p in pins)
        entry = {"min": minimum, "max": limit if star else maximum}
        if star:
            entry["max_symbol"] = limit_symbol
        entry["pins"] = [{k: v for k, v in pin.items() if not k.startswith("_")} for pin in pins]
        return entry

    if device:
        inputs_json = {"min": 0, "max": shape["max_in"], "pins": []}
        outputs_json = {"min": 0, "max": shape["max_q"], "pins": []}
    else:
        min_in, max_in = pin_shape(inputs, "CONFIG_VM_BLOCK_MAX_IN")
        min_q, max_q = pin_shape(outputs, "CONFIG_VM_BLOCK_MAX_OUT")
        inputs_json = group(inputs, "CONFIG_VM_BLOCK_MAX_IN", shape["min_in"], shape["max_in"])
        outputs_json = group(outputs, "CONFIG_VM_BLOCK_MAX_OUT", shape["min_q"], shape["max_q"])

    scope = {"inputs": {p["name"]: p for p in inputs}, "outputs": {p["name"]: p for p in outputs},
             "fields": {f["name"]: f for f in (state["fields"] if state else [])}}
    if device:
        scope["extra"] = {"op": "op", "device": "device"}
        scope["fields"] = {k: v for k, v in scope["fields"].items() if v.get("role") in (None, "device", "op")}
    block = {
        "schemaVersion": SCHEMA_VERSION, "kind": "vm-block", "id": block_id, "name": symbol,
        "alias": tags["alias"], "category": tags["category"], "description": tags["description"],
        "source_file": label, "guide": GUIDE_NAME.format(name=path.parent.name),
        "activation": {"kind": tags["activation"], "text": tags["when"]},
        "min_enables": 1 if enables == "required" else 0,
    }
    if loop in LOOP_USES:
        block["loop_use"] = LOOP_USES[loop]
    en = {}
    for key, tag in (("alias", "en-alias"), ("description", "en-description"), ("connect", "en-connect")):
        if tag in tags:
            en[key] = tag_one(tags, tag, context)
    if "en-signal" in tags:
        if tags["en-signal"] not in ("", "true"):
            fail(f"{context}: //@en-signal takes no value (or `true`)")
        en["signal"] = True
    if en:
        block["en"] = en
    eno = {}
    for key, tag in (("alias", "eno-alias"), ("description", "eno-description")):
        if tag in tags:
            eno[key] = tag_one(tags, tag, context)
    if eno:
        block["eno"] = eno
    if "face" in tags and not device:
        block["face"] = build_face(tags["face"], scope, context)
    elif "face" in tags and device:
        block["face"] = build_face(tags["face"], scope, context)
    if device:
        block["device"] = device
    block["shape"] = shape
    block["inputs"] = inputs_json
    block["outputs"] = outputs_json
    if ops is not None:
        block["op_field"] = roles["op"]["name"]
        block["idle_op_field"] = roles["idle-op"]["name"]
        block["const_field"] = roles["const"]["name"]
        block["idle_const_field"] = roles["idle-const"]["name"]
        block["device_field"] = roles["device"]["name"]
        block["ops"] = ops
    rules = []
    for entry in tags.get("rule", []):
        text_, rtags = parse_tags(entry["text"], RULE_TAGS, f"{context}: //@rule (line {entry['line']})")
        if not text_ or "error" not in rtags:
            fail(f"{context}: //@rule (line {entry['line']}) needs `<text> @error <ERR_TAG>`")
        if rtags["error"] not in error_tags():
            fail(f"{context}: //@rule (line {entry['line']}): {rtags['error']} is not an error tag of any ERR_*_MAP")
        rules.append({"rule": text_, "error": rtags["error"], "error_tag": error_tags()[rtags["error"]]})
    if rules:
        block["rules"] = rules
    if state:
        block["state"] = state
        block["min_custom_len"] = state["size"]
        if "data-tail" in tags:
            block["state"]["tail"] = tags["data-tail"]
    elif "data-tail" in tags:
        fail(f"{context}: //@data-tail needs //@data")
    used_enums = {}
    if state:
        for f in state["fields"]:
            if "enum_ref" in f:
                used_enums[f["enum_ref"]] = published_enum(enums[f["enum_ref"]], f["enum_ref"], context)
    for n, e in own_enums.items():
        if re.match(r"^vm_(in|out)_", n):
            continue
        if n not in used_enums:
            used_enums[n] = published_enum(e, n, context)
    if used_enums:
        block["enums"] = used_enums
    if "opcodes" in tags:
        if not state:
            fail(f"{context}: //@opcodes needs //@data")
        block["encoding"] = bytecode_encoding(tags, inputs, state, enums, scope, context)
    elif "example" in tags or "formula" in tags:
        fail(f"{context}: //@example / //@formula need //@opcodes")
    if state:
        validate_editor_links(block, context)
    return block


# ---------------------------------------------------------------------------
# Whole run
# ---------------------------------------------------------------------------

def build():
    blocks = []
    seen_ids = {}
    for path in sorted(BLOCKS_DIR.rglob("vm_block_*.h")):
        text = read(path)
        directive = scan_directive(rel(path), text)
        if directive is None:
            continue
        block = build_block(path, text, directive, enum_catalog())
        if block["id"] in seen_ids:
            fail(f"{block['name']} and {seen_ids[block['id']]} share id {block['id']}")
        seen_ids[block["id"]] = block["name"]
        guide = path.parent / GUIDE_NAME.format(name=path.parent.name)
        if not guide.exists():
            fail(f"{rel(path)}: the user guide {rel(guide)} is missing")
        block["_guide"] = guide
        blocks.append(block)
    blocks.sort(key=lambda b: b["id"])
    unused = sorted(set(block_ids()) - {b["name"] for b in blocks} - {"VM_BLK_NONE"})
    if unused:
        fail(f"{rel(IDS_HEADER)}: {', '.join(unused)} have no block header with `//#block`")
    return blocks


def index_of(blocks):
    return {
        "schemaVersion": SCHEMA_VERSION, "kind": "vm-blocks-index",
        "value_kinds": VALUE_KINDS, "activation_kinds": ACTIVATION_KINDS, "id_kinds": ID_KINDS, "op_kinds": OP_KINDS,
        "field_roles": FIELD_ROLES, "common_rules": COMMON_RULES,
        "blocks": [{"id": b["id"], "name": b["name"], "alias": b["alias"], "category": b["category"],
                    "file": f"{b['guide'][:-3]}.block.json", "guide": b["guide"]} for b in blocks],
    }


def render(document):
    return json.dumps(document, indent=2, ensure_ascii=False) + "\n"


def outputs():
    blocks = build()
    files = {}
    for block in blocks:
        guide = block.pop("_guide")
        stem = block["guide"][:-3]
        files[OUT_DIR / f"{stem}.block.json"] = render(block)
        files[OUT_DIR / block["guide"]] = read(guide)
    files[OUT_DIR / "index.generated.json"] = render(index_of(blocks))
    return files


def main(argv):
    check = "--check" in argv
    files = outputs()
    if check:
        stale = []
        for path, content in files.items():
            if not path.exists() or path.read_text(encoding="utf-8").replace("\r\n", "\n") != content:
                stale.append(rel(path))
        extra = [rel(p) for p in OUT_DIR.glob("*") if p.is_file() and p not in files] if OUT_DIR.exists() else []
        if stale or extra:
            print("out of date:", *stale, *[f"{e} (no longer generated)" for e in extra], sep="\n  ")
            return 1
        print("vm blocks: up to date")
        return 0
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for old in OUT_DIR.glob("*"):
        if old.is_file() and old not in files:
            old.unlink()
    for path, content in files.items():
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(content)
    print(f"Wrote {rel(OUT_DIR)}: {len(files) - 1} files for {sum(1 for p in files if p.name.endswith('.block.json'))} blocks and the index")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
