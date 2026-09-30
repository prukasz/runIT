"""Generate one descriptor per VM block, plus an index, for the app's block palette, block face and program compiler.

Every block header in components/VM/blocks/ describes its block in one
directive block: a `//#vm-block VM_BLK_<NAME>` line, then one `//@keyword ...`
line per fact, directly above the block's `#define VM_BLOCK_TYPE_<NAME>` macro.
Blank lines and plain `//` comments inside the block are fine. Anything that is
not understood is an error, so a typo can never silently drop a fact. Grammar:
vm-annotations.md.

  //#vm-block VM_BLK_TIMER
  //@title Timer
  //@category time
  //@activation enabled Runs every pass while enabled.
  //@data vm_block_timer_data_t                   private state struct (custom_data)
  //@data-tail <text>                             variable bytes after it (bytecode blocks)
  //@opcodes <enum>                               bytecode blocks: the opcode enum
  //@block-description <text>
  //@in  <index|*> <name> @title <text> @value <kind> [@description <text>] [@overrides <field>] [@macro <C index macro>] ...
  //@out <index|*> <name> @title <text> @value <kind> [@description <text>] [@macro <C index macro>]
  //@eno @title <text> [@description <text>]      what the block's ENO is called
  //@rule <text> @error <ERR_TAG>                 what the device checks at load
  //@example <title> @in <values> [@consts <values>] @code <opcodes and operands> @result <value>
  //@header <lead words> | {ref} {ref}            the block face's line under the title
  #define VM_BLOCK_TYPE_TIMER {.run = ..., .min_in = 1, .required_in = 0x1u, ...}

The block id comes from VM_BLK_<NAME> in vm_blocks.h and the shape (minimum
pins, required inputs) from the VM_BLOCK_TYPE_<NAME> macro the firmware checks
at load, so a descriptor cannot disagree with what the device accepts. The
private-state struct is laid out here (natural C alignment) and cross-checked
against its _Static_assert(sizeof) and every _Static_assert(offsetof) the
header states.

A pin with `@overrides <field>` reads a constant from that state field while it
is unwired: the pin is hidden until it is wired, and the field is the constant
the user enters. `{ref}` in a header names an input pin (its wired source, else
the constant), an output pin, a state field or `title`.

Usage:
  python data-structures/auto-annotations/vm/generate-vm-blocks.py [--check] [--skip-schema]
"""
import importlib.util
import json
import re
import struct
import sys
from pathlib import Path

try:
    import jsonschema
except ImportError:
    jsonschema = None

PROJECT_ROOT = Path(__file__).parents[3]
VM_ROOT = PROJECT_ROOT / "components" / "VM"
BLOCKS_DIR = VM_ROOT / "blocks"
OUT_DIR = PROJECT_ROOT / "data-structures" / "vm" / "blocks"
BLOCK_SCHEMA = PROJECT_ROOT / "data-structures" / "schema" / "vm-block.schema.json"
INDEX_SCHEMA = PROJECT_ROOT / "data-structures" / "schema" / "vm-blocks-index.schema.json"

ID_RE = re.compile(r"^#define\s+VM_BLK_(\w+)\s+(\d+)", re.MULTILINE)
START_RE = re.compile(r"^//#vm-block\b[ \t]*(?P<rest>.*)$")
DIRECTIVE_RE = re.compile(r"^//@(?P<keyword>[a-z][a-z0-9-]*)(?:[ \t]+(?P<rest>.*))?$")
TAG_RE = re.compile(r"(?:^|(?<=\s))@([a-z][a-z0-9-]*)(?=\s|$)")
PIN_RE = re.compile(r"^(?P<index>\*|\d+)\s+(?P<name>\w+)(?P<tags>(?:\s.*)?)$")
REF_RE = re.compile(r"\{(\w+)\}")
FIELD_RE = re.compile(r"^\s*(?P<type>[A-Za-z_][\w ]*?)\s+(?P<name>\w+)\s*(?:\[(?P<arr>\w*)\])?\s*;\s*(?://\s*(?P<comment>.*))?$")
OPTABLE_RE = r"//#vm-opcodes\s+{enum}\s*\n[^{{]*\{{(?P<body>.*?)\n\}};"
OPENTRY_RE = re.compile(r"\[(?P<sym>\w+)\]\s*=\s*\{\s*(?P<pops>\d+)\s*,\s*(?P<pushes>\d+)\s*,\s*VM_EXPR_ARG_(?P<arg>\w+)\s*\}")
ERROR_TAG_RE = re.compile(r"X\(\s*(ERR_\w+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,")
IDENT_RE = re.compile(r"^[A-Za-z_]\w*$")

# What each `//@keyword` line of a block takes: `one` once, `many` any number of times.
DIRECTIVES = {
    "title": "one", "category": "one", "activation": "one", "block-description": "one",
    "data": "one", "data-tail": "one", "opcodes": "one", "header": "one", "eno": "one", "always-detailed": "one",
    "rule": "many", "example": "many", "in": "many", "out": "many",
}
REQUIRED_DIRECTIVES = ("title", "category", "activation", "block-description")

# Tags per line kind. A tag outside its set, or given twice, is an error.
PIN_TAGS = {"title", "description", "value", "overrides", "macro", "hidden-by-default", "id", "device-field"}
FIELD_TAGS = {"enum-ref", "runtime", "derived", "id", "device-field", "contract", "hidden-by-default", "let-user-select-available", "dynamic-input"}
ENO_TAGS = {"title", "description"}
RULE_TAGS = {"error"}
EXAMPLE_TAGS = {"in", "consts", "code", "result"}
FLAG_TAGS = {"hidden-by-default", "runtime"}

# (size, alignment) of every type a state struct may use.
TYPES = {
    "uint8_t": (1, 1), "int8_t": (1, 1), "bool": (1, 1), "char": (1, 1),
    "uint16_t": (2, 2), "int16_t": (2, 2),
    "uint32_t": (4, 4), "int32_t": (4, 4), "float": (4, 4),
    "uint64_t": (8, 8), "int64_t": (8, 8), "double": (8, 8),
    "vm_span_t": (4, 2), "vm_edge_val_u": (4, 4), "vm_expr_k_t": (4, 4),
}

# What a pin reads or writes. Scalar kinds accept an object of any scalar type
# (B, U8, U32, I32, F, STR): the device converts on read and on write.
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
ARG_KINDS = {"NONE": "none", "INPUT": "input", "CONST": "const"}

# The C field types a pin of each value kind may take its constant from.
OVERRIDE_TYPES = {
    "bool": {"uint8_t", "bool"}, "u8": {"uint8_t"}, "u32": {"uint32_t", "uint16_t", "uint8_t"},
    "i32": {"int32_t", "int16_t", "int8_t"}, "f32": {"float"}, "scalar": set(TYPES),
}


def fail(message):
    raise SystemExit(f"ERROR: {message}")


# ---------------------------------------------------------------------------
# Lines and tags
# ---------------------------------------------------------------------------

def parse_tags(text, allowed, context):
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
        if name in FLAG_TAGS and value:
            fail(f"{context}: @{name} takes no value (got `{value}`)")
        tags[name] = value
    return lead, tags


def safe_int(expr, context):
    """An integer C constant expression (`0x1u`, `(1u << 0) | (1u << 2)`); nothing else."""
    text = re.sub(r"(?<=[0-9a-fA-F])[uUlL]+\b", "", expr.strip())
    if not text or not re.fullmatch(r"[0-9a-fA-FxX<>|&+()\s-]+", text):
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
    return [item for item in items if item.strip()]


# ---------------------------------------------------------------------------
# Finding the blocks in a header
# ---------------------------------------------------------------------------

def scan_blocks(label, text):
    """Every `//#vm-block` directive block of a header: name, line, directives and the VM_BLOCK_TYPE macro below it."""
    lines = text.replace("\r\n", "\n").split("\n")
    claimed, blocks, i = set(), [], 0
    while i < len(lines):
        m = START_RE.match(lines[i])
        if not m:
            i += 1
            continue
        where = f"{label}:{i + 1}"
        head = re.match(r"^VM_BLK_(\w+)$", m.group("rest").strip())
        if not head:
            fail(f"{where}: `//#vm-block` takes the block symbol alone (VM_BLK_<NAME>), one fact per following //@ line; got `{m.group('rest').strip()}`")
        block = {"name": head.group(1), "line": i + 1, "directives": []}
        claimed.add(i)
        j = i + 1
        while j < len(lines):
            stripped = lines[j].strip()
            if not stripped or (stripped.startswith("//") and not stripped.startswith("//#") and not stripped.startswith("//@")):
                j += 1
                continue
            if stripped.startswith("//@"):
                d = DIRECTIVE_RE.match(stripped)
                if not d:
                    fail(f"{label}:{j + 1}: malformed directive `{stripped}`")
                keyword = d.group("keyword")
                if keyword not in DIRECTIVES:
                    fail(f"{label}:{j + 1}: unknown directive //@{keyword} (known: {', '.join(sorted(DIRECTIVES))})")
                block["directives"].append({"keyword": keyword, "rest": (d.group("rest") or "").strip(), "line": j + 1})
                claimed.add(j)
                j += 1
                continue
            break
        if j >= len(lines) or not re.match(rf"#define\s+VM_BLOCK_TYPE_{block['name']}\b", lines[j]):
            fail(f"{where}: the //#vm-block block of VM_BLK_{block['name']} must end right above `#define VM_BLOCK_TYPE_{block['name']}` (found `{lines[j].strip() if j < len(lines) else 'end of file'}`)")
        block["macro"] = read_type_macro(lines, j, f"{label}:{j + 1}")
        blocks.append(block)
        i = j + 1
    for k, line in enumerate(lines):
        if line.lstrip().startswith("//@") and k not in claimed:
            fail(f"{label}:{k + 1}: `{line.strip()[:60]}` is not inside a //#vm-block directive block (put it below the //#vm-block line)")
    return blocks


def read_type_macro(lines, start, where):
    """min_in / min_q / required_in of `#define VM_BLOCK_TYPE_X { .min_in = .. }` (an omitted field is 0, as in C)."""
    parts, k = [], start
    while True:
        line = lines[k]
        if line.rstrip().endswith("\\"):
            parts.append(line.rstrip()[:-1])
            k += 1
            if k >= len(lines):
                fail(f"{where}: VM_BLOCK_TYPE macro continues past the end of the file")
            continue
        parts.append(line)
        break
    m = re.match(r"#define\s+VM_BLOCK_TYPE_\w+\s*(.*)$", " ".join(parts).strip())
    body = m.group(1).strip()
    if not (body.startswith("{") and body.endswith("}")):
        fail(f"{where}: VM_BLOCK_TYPE macro must be one `{{ .field = value, ... }}` initializer")
    fields = {}
    for item in split_top_level(body[1:-1]):
        f = re.match(r"\s*\.(\w+)\s*=\s*(.+?)\s*$", item, re.DOTALL)
        if not f:
            fail(f"{where}: cannot read initializer `{item.strip()}` (designated `.name = value` only)")
        fields[f.group(1)] = f.group(2)
    return {key: safe_int(fields.get(key, "0"), f"{where}: .{key}") for key in ("min_in", "min_q", "required_in")}


def header_macro(text, macro, context):
    m = re.search(rf"^#define\s+{re.escape(macro)}\s+([^\n/]+)", text, re.MULTILINE)
    if not m:
        fail(f"{context}: @macro {macro} is not defined in the header")
    return safe_int(m.group(1), f"{context}: {macro}")


# ---------------------------------------------------------------------------
# Environment
# ---------------------------------------------------------------------------

def load_enum_catalog():
    path = PROJECT_ROOT / "data-structures" / "auto-annotations" / "enums" / "generate-enums.py"
    spec = importlib.util.spec_from_file_location("generate_enums", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.scan(module.COMPONENTS_DIR)


def load_errors():
    errors = {}
    for h in (PROJECT_ROOT / "components").rglob("*.h"):
        for name, value in ERROR_TAG_RE.findall(h.read_text(encoding="utf-8", errors="ignore")):
            errors[name] = int(value, 16)
    return errors


def load_sdkconfig():
    path = PROJECT_ROOT / "sdkconfig"
    if not path.exists():
        fail(f"{path} not found: configure the firmware once (idf.py reconfigure) so the Kconfig limits can be read")
    out = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        m = re.match(r"^(CONFIG_\w+)=(0x[0-9a-fA-F]+|-?\d+)$", line)
        if m:
            out[m.group(1)] = int(m.group(2), 0)
    return out


# ---------------------------------------------------------------------------
# State struct
# ---------------------------------------------------------------------------

def find_struct(name):
    """(body, align_attr, static_size, source path, header text) of `typedef struct ... { body } name;` under components/VM."""
    pattern = re.compile(r"typedef\s+struct\s*(?P<attr>__attribute__\s*\(\(\s*aligned\((?P<align>\d+)\)\s*\)\))?\s*\w*\s*\{(?P<body>[^{}]*)\}\s*" + re.escape(name) + r"\s*;")
    for path in sorted(VM_ROOT.rglob("*.h")):
        text = path.read_text(encoding="utf-8", errors="ignore")
        m = pattern.search(text)
        if m:
            size = re.search(r"_Static_assert\(\s*sizeof\(\s*" + re.escape(name) + r"\s*\)\s*==\s*(\d+)", text)
            return m.group("body"), int(m.group("align") or 1), int(size.group(1)) if size else None, path, text
    fail(f"state struct {name} not found under components/VM")


def layout_state(name, enums, context):
    body, attr_align, static_size, source, source_text = find_struct(name)
    if static_size is None:
        fail(f"{context}: {name} needs `_Static_assert(sizeof({name}) == N, ...)` so its layout is checked against the compiler")
    fields, offset, max_align = [], 0, attr_align
    for line in body.splitlines():
        if not line.strip() or line.strip().startswith("//"):
            continue
        m = FIELD_RE.match(line)
        if not m:
            fail(f"{context}: cannot parse state field `{line.strip()}` in {name}")
        c_type = " ".join(m.group("type").split())
        if c_type not in TYPES:
            fail(f"{context}: state field type `{c_type}` has no known size (add it to TYPES)")
        size, align = TYPES[c_type]
        offset = (offset + align - 1) // align * align
        max_align = max(max_align, align)
        field_ctx = f"{context}.{m.group('name')}"
        comment = m.group("comment") or ""
        description, tags = parse_tags(comment, FIELD_TAGS, field_ctx)
        field = {"name": m.group("name"), "c_type": c_type, "offset": offset}
        arr = m.group("arr")
        if arr is None:
            field["size"] = size
            offset += size
        elif arr == "":
            field["flexible"] = True
            field["element_size"] = size
        else:
            if not re.fullmatch(r"\d+|0[xX][0-9a-fA-F]+", arr):
                fail(f"{field_ctx}: array length `{arr}` must be a number")
            count = int(arr, 0)
            field.update(size=size * count, array_len=count, element_size=size)
            offset += size * count
        if description:
            field["description"] = description
        field.update(field_metadata(tags, field_ctx))
        if "enum-ref" in tags:
            if tags["enum-ref"] not in enums:
                fail(f"{field_ctx}: @enum-ref {tags['enum-ref']} is not a published //#ref-enum")
            field["enum_ref"] = tags["enum-ref"]
        # Who writes the bytes: the app from its settings (user), the app from
        # the program's layout (derived), the device (runtime, app writes 0).
        if field["name"].startswith("_"):
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
    return {"struct": name, "source_file": source.relative_to(PROJECT_ROOT).as_posix(), "size": total, "align": max_align, "fields": fields}


def field_metadata(tags, context):
    """Editor metadata of a state field or a pin (`@id`, `@device-field`, ...), checked for shape."""
    metadata = {}
    if "id" in tags:
        if tags["id"] not in ("device", "pin"):
            fail(f"{context}: @id must be device or pin")
        metadata["id_kind"] = tags["id"]
    for tag in ("device-field", "contract", "let-user-select-available"):
        if tag in tags:
            if not IDENT_RE.match(tags[tag]):
                fail(f"{context}: @{tag} needs one identifier")
            metadata[tag.replace("-", "_")] = tags[tag]
    if "hidden-by-default" in tags:
        metadata["hidden_by_default"] = True
    if "dynamic-input" in tags:
        if not re.fullmatch(r"\d+", tags["dynamic-input"]):
            fail(f"{context}: @dynamic-input needs an input index")
        metadata["dynamic_input"] = int(tags["dynamic-input"])
    return metadata


# ---------------------------------------------------------------------------
# Pins
# ---------------------------------------------------------------------------

def parse_pins(directives, shape, text, sdkconfig, context):
    """(inputs, outputs) from the //@in and //@out lines, checked against the macro's shape and the header's index macros."""
    pins = {"in": [], "out": []}
    for d in directives:
        if d["keyword"] not in pins:
            continue
        where = f"{context}: //@{d['keyword']} (line {d['line']})"
        m = PIN_RE.match(d["rest"])
        if not m:
            fail(f"{where}: expected `<index|*> <name> @title <text> @value <kind>`, got `{d['rest']}`")
        lead, tags = parse_tags(m.group("tags"), PIN_TAGS, where)
        if lead:
            fail(f"{where}: unexpected text `{lead}` before the first tag")
        if "title" not in tags or not tags["title"]:
            fail(f"{where}: needs @title")
        if tags.get("value") not in VALUE_KINDS:
            fail(f"{where}: needs @value, one of {sorted(VALUE_KINDS)}")
        index = m.group("index")
        pin = {"index": "*" if index == "*" else int(index), "name": m.group("name"), "title": tags["title"], "value": tags["value"]}
        if "description" in tags:
            if not tags["description"]:
                fail(f"{where}: @description is empty")
            pin["description"] = tags["description"]
        pin.update(field_metadata({k: v for k, v in tags.items() if k in ("id", "device-field", "hidden-by-default")}, where))
        if "overrides" in tags:
            if not IDENT_RE.match(tags["overrides"]):
                fail(f"{where}: @overrides needs one state field name")
            pin["overrides"] = tags["overrides"]
        if "macro" in tags:
            expected = header_macro(text, tags["macro"], where)
            if expected != pin["index"]:
                fail(f"{where}: the header says {tags['macro']} is {expected}, the annotation says {pin['index']}")
        pin["_where"] = where
        pins[d["keyword"]].append(pin)

    names = [p["name"] for side in pins.values() for p in side]
    for name in sorted({n for n in names if names.count(n) > 1}):
        fail(f"{context}: pin name `{name}` is used twice (pin names are unique across the block so {{{name}}} is unambiguous)")

    for side, limit_key, minimum in (("in", "CONFIG_VM_BLOCK_MAX_IN", shape["min_in"]), ("out", "CONFIG_VM_BLOCK_MAX_OUT", shape["min_q"])):
        listed = pins[side]
        numbered = sorted(p["index"] for p in listed if p["index"] != "*")
        stars = [p for p in listed if p["index"] == "*"]
        if len(numbered) != len(set(numbered)):
            fail(f"{context}: two //@{side} lines share an index")
        if len(stars) > 1:
            fail(f"{context}: at most one `*` //@{side} line")
        if numbered != list(range(len(numbered))):
            missing = sorted(set(range(numbered[-1] + 1)) - set(numbered)) if numbered else []
            fail(f"{context}: //@{side} indexes must run 0..{len(numbered) - 1} without gaps (missing {missing or 'a start at 0'})")
        if numbered and numbered[-1] >= sdkconfig[limit_key]:
            fail(f"{context}: //@{side} {numbered[-1]} is past {limit_key} ({sdkconfig[limit_key]})")
        if stars and listed.index(stars[0]) != len(listed) - 1:
            fail(f"{context}: the `*` //@{side} line must come last")
        if minimum > len(numbered) and not stars:
            fail(f"{context}: the macro needs {minimum} {'inputs' if side == 'in' else 'outputs'} but only {len(numbered)} are annotated")
        if side == "in":
            for pin_index in range(minimum):
                if pin_index not in numbered and not stars:
                    fail(f"{context}: input {pin_index} is below min_in but has no //@in line")
    required = shape["required_in"]
    for bit in range(required.bit_length()):
        if required & (1 << bit) and not any(p["index"] == bit for p in pins["in"]):
            fail(f"{context}: required input {bit} has no //@in annotation")
    for pin in pins["in"]:
        pin["required"] = pin["index"] != "*" and bool(required & (1 << pin["index"]))
    return pins["in"], pins["out"]


def link_overrides(inputs, fields, context):
    """Tie each `@overrides` pin to its state field: the field is the constant used while the pin is unwired, so the pin starts hidden."""
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
        pin["hidden_by_default"] = True


def validate_editor_links(block, context):
    fields = {field["name"]: field for field in block.get("state", {}).get("fields", [])}
    pins = block["inputs"]["pins"]
    dynamic_targets = {f["dynamic_input"] for f in fields.values() if "dynamic_input" in f}
    for entry in [*fields.values(), *pins, *block["outputs"]["pins"]]:
        device = entry.get("device_field")
        if device and fields.get(device, {}).get("id_kind") != "device":
            fail(f"{context}.{entry['name']}: @device-field {device} must name a device ID field")
        if entry.get("id_kind") == "pin" and not device:
            fail(f"{context}.{entry['name']}: a pin ID needs @device-field")
        if entry.get("hidden_by_default") and entry.get("required"):
            fail(f"{context}.{entry['name']}: a required input cannot be hidden by default")
        selection = entry.get("let_user_select_available")
        if selection:
            if fields.get(selection, {}).get("id_kind") != "pin" or entry.get("c_type") != "uint64_t":
                fail(f"{context}.{entry['name']}: a selectable mask must be uint64_t and name a pin ID field")
            if not any(pin["index"] == entry.get("dynamic_input") and pin.get("hidden_by_default") for pin in pins):
                fail(f"{context}.{entry['name']}: @dynamic-input must name a hidden optional input")
    for pin in pins:
        if pin.get("hidden_by_default") and not pin.get("overrides") and pin["index"] not in dynamic_targets:
            fail(f"{context}.{pin['name']}: a hidden input needs `@overrides <field>` (the constant to use) or a mask field with @dynamic-input {pin['index']}; otherwise nothing can set it")


def pin_group(minimum, pins, limit_key, sdkconfig):
    """min / max counts of a pin direction: numbered pins cap it, a `*` pin lets it grow to the Kconfig limit."""
    limit = sdkconfig[limit_key]
    numbered = [p["index"] for p in pins if p["index"] != "*"]
    star = any(p["index"] == "*" for p in pins)
    maximum = limit if star else (max(numbered) + 1 if numbered else 0)
    group = {"min": minimum, "max": maximum}
    if star:
        group["max_symbol"] = limit_key
    group["pins"] = [{k: v for k, v in pin.items() if not k.startswith("_") and k != "macro"} for pin in pins]
    return group


# ---------------------------------------------------------------------------
# Face: the header template
# ---------------------------------------------------------------------------

def resolve_ref(name, scope, context):
    if name == "title":
        return {"ref": name, "kind": "title"}
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
    known = sorted({"title", *scope["inputs"], *scope["outputs"], *(n for n, f in scope["fields"].items() if f["source"] == "user")})
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


def build_header(directives, inputs, outputs, fields, context):
    """The block face's line under the title (`//@header <lead> | <value>`), or None."""
    scope = {"inputs": {p["name"]: p for p in inputs}, "outputs": {p["name"]: p for p in outputs}, "fields": {f["name"]: f for f in fields}}
    header_lines = [d for d in directives if d["keyword"] == "header"]
    if not header_lines:
        return None
    d = header_lines[0]
    where = f"{context}: //@header (line {d['line']})"
    lead, _, value = d["rest"].partition("|")
    header = {"lead": parse_template(lead, scope, where)}
    if value.strip():
        header["value"] = parse_template(value, scope, where)
    return header


# ---------------------------------------------------------------------------
# Bytecode encoding (EXPR / EXPR_BIT)
# ---------------------------------------------------------------------------

def opcode_table(text, enum_name, catalog, context):
    m = re.search(OPTABLE_RE.format(enum=re.escape(enum_name)), text, re.DOTALL)
    if not m:
        fail(f"{context}: @opcodes {enum_name} but no //#vm-opcodes {enum_name} table in the header")
    entries = {e.group("sym"): e for e in OPENTRY_RE.finditer(m.group("body"))}
    if enum_name not in catalog["enums"]:
        fail(f"{context}: {enum_name} is not a published //#ref-enum")
    ops = []
    for member in catalog["enums"][enum_name]["members"]:
        if member["name"].endswith("_OP_CNT"):
            continue
        e = entries.get(member["name"])
        if not e:
            fail(f"{context}: opcode {member['name']} has no entry in the //#vm-opcodes table")
        ops.append({"symbol": member["name"], "value": member["value"], "alias": member["alias"], "description": member["description"],
                    "pops": int(e.group("pops")), "pushes": int(e.group("pushes")), "operand": ARG_KINDS[e.group("arg")]})
    extra = set(entries) - {o["symbol"] for o in ops}
    if extra:
        fail(f"{context}: //#vm-opcodes table names non-members {sorted(extra)}")
    return ops


def check_code(code, ops, in_cnt, const_cnt, stack_max):
    """The device's load-time check (vm_expr_check), for the examples."""
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


def build_examples(directives, ops, const_type, stack_max, state, context):
    """Golden custom_data for each //@example: the state header is written into the computed layout, then constants, then code."""
    fields = {f["name"]: f for f in state["fields"]}
    for needed, width in (("const_cnt", 1), ("code_len", 2)):
        if needed not in fields or fields[needed].get("size") != width:
            fail(f"{context}: a bytecode block's state struct needs a {width}-byte `{needed}` field")
    symbols = {o["symbol"]: o for o in ops}
    out = []
    for d in directives:
        if d["keyword"] != "example":
            continue
        where = f"{context}: //@example (line {d['line']})"
        title, tags = parse_tags(d["rest"], EXAMPLE_TAGS, where)
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
        out.append({
            "title": title, "inputs": inputs, "constants": consts, "code": tokens,
            "custom_data": custom.hex(), "custom_len": len(custom),
            "result": constant(tags["result"], const_type, where),
        })
    return out


def bytecode_encoding(text, opcodes_enum, directives, inputs, state, catalog, context):
    ops = opcode_table(text, opcodes_enum, catalog, context)
    stack = re.search(r"#define\s+VM_EXPR_STACK_MAX\s+(\d+)", text)
    if not stack:
        fail(f"{context}: VM_EXPR_STACK_MAX not found")
    stack_max = int(stack.group(1))
    star = next((p for p in inputs if p["index"] == "*"), None)
    const_type = star["value"] if star else "f32"
    if const_type not in ("f32", "u32"):
        fail(f"{context}: a bytecode block's repeating input must be f32 or u32 (its constants take that type), not {const_type}")
    return {
        "kind": "bytecode",
        "layout": f"state header, then const_cnt constants of 4 bytes, then code_len code bytes; custom_len = {state['size']} + 4 x const_cnt + code_len",
        "constant_type": const_type,
        "stack_max": stack_max,
        "opcode_enum": opcodes_enum,
        "opcodes": ops,
        "examples": build_examples(directives, ops, const_type, stack_max, state, context),
    }


# ---------------------------------------------------------------------------
# Documents
# ---------------------------------------------------------------------------

def one(directives, keyword):
    found = [d for d in directives if d["keyword"] == keyword]
    return found[0] if found else None


def build_block(path, text, source, ids, catalog, errors, sdkconfig):
    enums = catalog["enums"]
    name = source["name"]
    context = f"{path.name}: VM_BLK_{name}"
    if name not in ids:
        fail(f"{context}: no #define VM_BLK_{name} in vm_blocks.h")
    directives = source["directives"]
    for keyword, times in DIRECTIVES.items():
        count = sum(1 for d in directives if d["keyword"] == keyword)
        if times == "one" and count > 1:
            fail(f"{context}: //@{keyword} given {count} times")
    for required in REQUIRED_DIRECTIVES:
        if not one(directives, required):
            fail(f"{context}: needs //@{required}")

    title = one(directives, "title")["rest"]
    category = one(directives, "category")["rest"]
    if not title or not re.fullmatch(r"[a-z][a-z0-9_-]*", category):
        fail(f"{context}: //@title needs text and //@category one lowercase word")
    kind, _, activation_text = one(directives, "activation")["rest"].partition(" ")
    if kind not in ACTIVATION_KINDS:
        fail(f"{context}: //@activation must start with one of {sorted(ACTIVATION_KINDS)}")
    description = one(directives, "block-description")["rest"]
    if not description:
        fail(f"{context}: //@block-description is empty")
    if one(directives, "data-tail") and not one(directives, "data"):
        fail(f"{context}: //@data-tail needs //@data")
    opcodes_enum = one(directives, "opcodes")["rest"] if one(directives, "opcodes") else None
    if opcodes_enum and not one(directives, "data"):
        fail(f"{context}: //@opcodes needs //@data (the header struct)")

    shape = source["macro"]
    inputs, outputs = parse_pins(directives, shape, text, sdkconfig, context)
    state = None
    if one(directives, "data"):
        state = layout_state(one(directives, "data")["rest"], enums, context)
        if one(directives, "data-tail"):
            state["tail"] = one(directives, "data-tail")["rest"]
    fields = state["fields"] if state else []
    link_overrides(inputs, fields, context)
    header = build_header(directives, inputs, outputs, fields, context)

    rules = []
    for d in directives:
        if d["keyword"] != "rule":
            continue
        where = f"{context}: //@rule (line {d['line']})"
        rule_text, tags = parse_tags(d["rest"], RULE_TAGS, where)
        err = tags.get("error")
        if not rule_text or err not in errors:
            fail(f"{where}: needs text and an existing @error (got {err})")
        rules.append({"rule": rule_text, "error": err, "error_tag": errors[err]})

    block = {
        "$schema": BLOCK_SCHEMA.relative_to(PROJECT_ROOT).as_posix(),
        "schemaVersion": 1,
        "kind": "vm-block",
        "id": ids[name],
        "name": f"VM_BLK_{name}",
        "title": title,
        "category": category,
        "description": description,
        "source_file": path.relative_to(PROJECT_ROOT).as_posix(),
        "activation": {"kind": kind, "description": activation_text.strip() or ACTIVATION_KINDS[kind]},
        "inputs": pin_group(shape["min_in"], inputs, "CONFIG_VM_BLOCK_MAX_IN", sdkconfig),
        "outputs": pin_group(shape["min_q"], outputs, "CONFIG_VM_BLOCK_MAX_OUT", sdkconfig),
        "rules": rules,
    }
    eno = one(directives, "eno")
    if eno:
        lead, tags = parse_tags(eno["rest"], ENO_TAGS, f"{context}: //@eno")
        if lead or not tags.get("title"):
            fail(f"{context}: //@eno needs @title (and no text before it)")
        block["eno"] = {"title": tags["title"], **({"description": tags["description"]} if tags.get("description") else {})}
    if header:
        block["header"] = header
    if one(directives, "always-detailed"):
        if one(directives, "always-detailed")["rest"].strip():
            fail(f"{context}: //@always-detailed takes no text")
        if not any(pin.get("overrides") for pin in inputs) and not opcodes_enum:
            fail(f"{context}: //@always-detailed needs an @overrides input or @opcodes: nothing else is in the detailed view")
        block["always_detailed"] = True
    if state:
        block["state"] = state
    block["min_custom_len"] = state["size"] if state else 0
    if opcodes_enum:
        block["encoding"] = bytecode_encoding(text, opcodes_enum, directives, inputs, state, catalog, context)
    used = sorted({f["enum_ref"] for f in fields if "enum_ref" in f} | ({opcodes_enum} if opcodes_enum else set()))
    block["enums"] = {e: enums[e] for e in used}
    validate_editor_links(block, context)
    return block


def build():
    ids = {name: int(value) for name, value in ID_RE.findall((BLOCKS_DIR / "vm_blocks.h").read_text(encoding="utf-8"))}
    catalog = load_enum_catalog()
    errors = load_errors()
    sdkconfig = load_sdkconfig()
    blocks = []
    for path in sorted(BLOCKS_DIR.glob("vm_block_*.h")):
        text = path.read_text(encoding="utf-8").replace("\r\n", "\n")
        for source in scan_blocks(path.name, text):
            blocks.append(build_block(path, text, source, ids, catalog, errors, sdkconfig))
    seen = {}
    for block in blocks:
        if block["name"] in seen:
            fail(f"{block['name']} has two //#vm-block directive blocks ({seen[block['name']]} and {block['source_file']})")
        seen[block["name"]] = block["source_file"]
    missing = sorted(f"VM_BLK_{n}" for n, v in ids.items() if v != 0 and f"VM_BLK_{n}" not in seen)
    if missing:
        fail(f"palette ids without a //#vm-block directive: {missing}")
    blocks.sort(key=lambda b: b["id"])
    index = {
        "$schema": INDEX_SCHEMA.relative_to(PROJECT_ROOT).as_posix(),
        "schemaVersion": 1,
        "kind": "vm-blocks-index",
        "common_rules": [
            "in_cnt and q_cnt are at least the block's inputs.min / outputs.min, and at most its max.",
            "Every required input is wired (not VM_BLOCK_NO_ID).",
            "custom_len is at least the state size.",
        ],
        "common_rules_error": "ERR_VM_BLK_BAD_SHAPE",
        "value_kinds": VALUE_KINDS,
        "activation_kinds": ACTIVATION_KINDS,
        "blocks": [{"id": b["id"], "name": b["name"], "title": b["title"], "category": b["category"], "file": file_name(b)} for b in blocks],
    }
    return blocks, index


def file_name(block):
    return "block_" + block["name"][len("VM_BLK_"):].lower() + ".generated.json"


def validate(document, schema_path, label):
    if jsonschema is None:
        fail("the jsonschema package is required to check the generated files (pip install jsonschema), or pass --skip-schema")
    schema = json.loads(schema_path.read_text(encoding="utf-8"))
    problems = sorted(jsonschema.Draft202012Validator(schema).iter_errors(document), key=lambda e: list(e.path))
    if problems:
        fail(f"{label} failed schema validation:\n" + "\n".join(f"  - at {'/'.join(map(str, e.path)) or '<root>'}: {e.message}" for e in problems))


def render(document):
    return (json.dumps(document, indent=2) + "\n").encode("utf-8")


def main(argv):
    check, skip_schema = "--check" in argv, "--skip-schema" in argv
    blocks, index = build()
    if not skip_schema:
        for block in blocks:
            validate(block, BLOCK_SCHEMA, block["name"])
        validate(index, INDEX_SCHEMA, "index")
    outputs = {OUT_DIR / file_name(b): render(b) for b in blocks}
    outputs[OUT_DIR / "index.generated.json"] = render(index)
    stale = [p for p in OUT_DIR.glob("*.generated.json") if p not in outputs] if OUT_DIR.exists() else []
    if check:
        # A Windows checkout may hold CRLF: compare the text, not the line endings.
        def current(path):
            return path.read_bytes().replace(b"\r\n", b"\n")

        wrong = [p.name for p, content in outputs.items() if not p.exists() or current(p) != content] + [f"{p.name} (no such block)" for p in stale]
        if wrong:
            print("Out of date: " + ", ".join(wrong) + "\nRun: python data-structures/auto-annotations/generate-all.py")
            return 1
        print(f"{OUT_DIR.relative_to(PROJECT_ROOT)} is up to date ({len(blocks)} blocks + index)")
        return 0
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for path in stale:
        path.unlink()  # a block that no longer exists
    for path, content in outputs.items():
        path.write_bytes(content)  # LF on every platform, so the files diff the same everywhere
    print(f"Wrote {OUT_DIR.relative_to(PROJECT_ROOT)} ({len(blocks)} blocks + index)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
