"""Generate one descriptor per VM block, plus an index, for the app's block palette and program compiler.

Every block header in components/VM/blocks/ carries, right above its palette
entry macro:

  //#vm-block VM_BLK_<NAME> @title <text> @category <word> @activation <kind> <text>
             [@state <struct>] [@state-tail <text>] [@opcodes <enum>]
  //@block-description <text>
  //@rule <text> @error <ERR_TAG>                      (what the device checks at load)
  //@example <title> @in <values> [@consts <values>] @code <opcode symbols and operands> @result <value>
  //@in <index|*> <name> @title <text> [@description <text>] @value <kind>
  //@out <index|*> <name> @title <text> [@description <text>] @value <kind>
  #define VM_BLOCK_TYPE_<NAME> {.run = ..., .min_in = N, .min_q = N, .required_in = 0x..u, .state_len = ...}

The block id comes from VM_BLK_<NAME> in vm_blocks.h and the shape (minimum
pins, required inputs) from the same VM_BLOCK_TYPE_<NAME> the firmware checks
at load, so a descriptor cannot disagree with what the device accepts. The
private-state struct is laid out here (natural C alignment, which every state
struct follows with explicit padding) and cross-checked against its
_Static_assert(sizeof(...) == N). Field comments: text before the first tag is
the description; `@enum-ref <enum>` names a published //#ref-enum; `@runtime`
marks bytes the device owns (the app writes 0); `@derived <rule>` marks bytes
the app computes from the program's layout.

A block with `@opcodes <enum>` carries a bytecode program after its state
header. Its opcode table (`//#vm-opcodes <enum>` above a
`[SYMBOL] = {pops, pushes, VM_EXPR_ARG_*}` initializer, the table the device's
load-time check uses) is published with the encoding, and every //@example is
assembled, checked against that table and published as ready bytes.

Usage:
  python data-structures/auto-annotations/vm/generate-vm-blocks.py
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
BLOCK_RE = re.compile(r"^//#vm-block\s+VM_BLK_(\w+)(?P<tags>[^\n]*)\n(?P<lines>(?://@[^\n]*\n)*)", re.MULTILINE)
TYPE_RE = r"#define\s+VM_BLOCK_TYPE_{name}\s*\\\s*\n\s*\{{(?P<body>[^}}]*)\}}"
TAG_RE = re.compile(r"@([\w-]+)\b")
PIN_RE = re.compile(r"^(?P<dir>in|out)\s+(?P<index>\*|\d+)\s+(?P<name>\w+)(?P<tags>.*)$")
FIELD_RE = re.compile(r"^\s*(?P<type>[A-Za-z_][\w ]*?)\s+(?P<name>\w+)\s*(?:\[(?P<arr>\w*)\])?\s*;\s*(?://\s*(?P<comment>.*))?$")
OPTABLE_RE = r"//#vm-opcodes\s+{enum}\s*\n[^{{]*\{{(?P<body>.*?)\n\}};"
OPENTRY_RE = re.compile(r"\[(?P<sym>\w+)\]\s*=\s*\{\s*(?P<pops>\d+)\s*,\s*(?P<pushes>\d+)\s*,\s*VM_EXPR_ARG_(?P<arg>\w+)\s*\}")
ERROR_TAG_RE = re.compile(r"X\(\s*(ERR_\w+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,")

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


def fail(message):
    raise SystemExit(f"ERROR: {message}")


def parse_tags(text):
    tags, matches = {}, list(TAG_RE.finditer(text))
    for i, m in enumerate(matches):
        end = matches[i + 1].start() if i + 1 < len(matches) else len(text)
        tags[m.group(1).lower().replace("-", "_")] = text[m.end():end].strip()
    return tags


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
    out = {}
    for line in (PROJECT_ROOT / "sdkconfig").read_text(encoding="utf-8").splitlines():
        m = re.match(r"^(CONFIG_\w+)=(\d+)$", line)
        if m:
            out[m.group(1)] = int(m.group(2))
    return out


def find_struct(name):
    """(body, align_attr, static_size, source) of `typedef struct ... { body } name;` under components/VM."""
    pattern = re.compile(r"typedef\s+struct\s*(?P<attr>__attribute__\s*\(\(\s*aligned\((?P<align>\d+)\)\s*\)\))?\s*\w*\s*\{(?P<body>[^{}]*)\}\s*" + re.escape(name) + r"\s*;")
    for path in sorted(VM_ROOT.rglob("*.h")):
        text = path.read_text(encoding="utf-8", errors="ignore")
        m = pattern.search(text)
        if m:
            size = re.search(r"_Static_assert\(\s*sizeof\(\s*" + re.escape(name) + r"\s*\)\s*==\s*(\d+)", text)
            return m.group("body"), int(m.group("align") or 1), int(size.group(1)) if size else None, path.relative_to(PROJECT_ROOT).as_posix()
    fail(f"state struct {name} not found under components/VM")


def layout_state(name, enums, context):
    body, attr_align, static_size, source = find_struct(name)
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
        comment = m.group("comment") or ""
        tags = parse_tags(comment)
        description = comment.split("@", 1)[0].strip()
        field = {"name": m.group("name"), "c_type": c_type, "offset": offset}
        arr = m.group("arr")
        if arr is None:
            field["size"] = size
            offset += size
        elif arr == "":
            field["flexible"] = True
            field["element_size"] = size
        else:
            count = int(arr, 0)
            field.update(size=size * count, array_len=count, element_size=size)
            offset += size * count
        if description:
            field["description"] = description
        if "enum_ref" in tags:
            if tags["enum_ref"] not in enums:
                fail(f"{context}: {name}.{field['name']} @enum-ref {tags['enum_ref']} is not a published //#ref-enum")
            field["enum_ref"] = tags["enum_ref"]
        # Who writes the bytes: the app from its settings (user), the app from
        # the program's layout (derived), the device (runtime, app writes 0).
        if field["name"].startswith("_"):
            field["source"] = "padding"
        elif "runtime" in tags:
            field["source"] = "runtime"
        elif "derived" in tags:
            field["source"] = "derived"
            field["derived"] = tags["derived"]
        else:
            field["source"] = "user"
        fields.append(field)
    total = (offset + max_align - 1) // max_align * max_align
    if static_size is not None and static_size != total:
        fail(f"{context}: computed size of {name} is {total}, _Static_assert says {static_size}")
    return {"struct": name, "source_file": source, "size": total, "align": max_align, "fields": fields}


def parse_int(expr):
    expr = expr.strip().rstrip("uU")
    return int(expr, 0)


def parse_type_macro(text, name, context):
    m = re.search(TYPE_RE.format(name=name), text)
    if not m:
        fail(f"{context}: VM_BLOCK_TYPE_{name} macro not found next to its //#vm-block directive")
    fields = dict(re.findall(r"\.(\w+)\s*=\s*([^,]+)", m.group("body")))
    return {"min_in": parse_int(fields["min_in"]), "min_q": parse_int(fields["min_q"]), "required_in": parse_int(fields["required_in"])}


def parse_pins(lines, required_in, context):
    inputs, outputs = [], []
    for raw in lines:
        m = PIN_RE.match(raw)
        if not m:
            continue
        tags = parse_tags(m.group("tags"))
        if "title" not in tags:
            fail(f"{context}: pin `{raw}` needs @title")
        if tags.get("value") not in VALUE_KINDS:
            fail(f"{context}: pin `{raw}` needs @value, one of {sorted(VALUE_KINDS)}")
        index = m.group("index")
        pin = {"index": "*" if index == "*" else int(index), "name": m.group("name"), "title": tags["title"], "value": tags["value"]}
        if tags.get("description"):
            pin["description"] = tags["description"]
        if m.group("dir") == "in":
            pin["required"] = index != "*" and bool(required_in & (1 << int(index)))
            inputs.append(pin)
        else:
            outputs.append(pin)
    for bit in range(16):
        if required_in & (1 << bit) and not any(p["index"] == bit for p in inputs):
            fail(f"{context}: required input {bit} has no //@in annotation")
    return inputs, outputs


def pin_group(minimum, pins, limit_key, sdkconfig):
    """min / max counts of a pin direction: numbered pins cap it, a `*` pin lets it grow to the Kconfig limit."""
    limit = sdkconfig[limit_key]
    numbered = [p["index"] for p in pins if p["index"] != "*"]
    maximum = limit if any(p["index"] == "*" for p in pins) else (max(numbered) + 1 if numbered else 0)
    return {"min": minimum, "max": maximum, **({"max_symbol": limit_key} if maximum == limit and any(p["index"] == "*" for p in pins) else {}), "pins": pins}


def parse_rules(lines, errors, context):
    rules = []
    for line in lines:
        if not line.startswith("rule "):
            continue
        tags = parse_tags(line[len("rule "):])
        text = line[len("rule "):].split("@error", 1)[0].strip()
        err = tags.get("error")
        if not text or err not in errors:
            fail(f"{context}: //@rule needs text and an existing @error (got {err})")
        rules.append({"rule": text, "error": err, "error_tag": errors[err]})
    return rules


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


def build_examples(lines, ops, const_type, stack_max, context):
    symbols = {o["symbol"]: o for o in ops}
    out = []
    for line in lines:
        if not line.startswith("example "):
            continue
        tags = parse_tags(line[len("example "):])
        title = line[len("example "):].split("@", 1)[0].strip()
        inputs = number_list(tags.get("in", ""))
        consts = number_list(tags.get("consts", ""))
        code = []
        tokens = tags.get("code", "").split()
        i = 0
        while i < len(tokens):
            op = symbols.get(tokens[i])
            if not op:
                fail(f"{context}: example '{title}': unknown opcode {tokens[i]}")
            code.append(op["value"])
            i += 1
            if op["operand"] != "none":
                code.append(int(tokens[i], 0))
                i += 1
        problem = check_code(code, ops, len(inputs), len(consts), stack_max)
        if problem:
            fail(f"{context}: example '{title}' would be refused at load: {problem}")
        packed = b"".join(struct.pack("<f", float(c)) if const_type == "f32" else struct.pack("<I", int(c, 0)) for c in consts)
        state = struct.pack("<BBH", len(consts), 0, len(code)) + packed + bytes(code)
        out.append({
            "title": title,
            "inputs": [float(v) if const_type == "f32" else int(v, 0) for v in inputs],
            "constants": [float(c) if const_type == "f32" else int(c, 0) for c in consts],
            "code": tokens,
            "custom_data": state.hex(),
            "custom_len": len(state),
            "result": float(tags["result"]) if const_type == "f32" else int(tags["result"], 0),
        })
    return out


def bytecode_encoding(text, tags, lines, inputs, catalog, context):
    ops = opcode_table(text, tags["opcodes"], catalog, context)
    stack = re.search(r"#define\s+VM_EXPR_STACK_MAX\s+(\d+)", text)
    if not stack:
        fail(f"{context}: VM_EXPR_STACK_MAX not found")
    stack_max = int(stack.group(1))
    const_type = inputs[0]["value"] if inputs else "f32"
    return {
        "kind": "bytecode",
        "layout": "state header, then const_cnt constants of 4 bytes, then code_len code bytes; custom_len = 4 + 4 x const_cnt + code_len",
        "constant_type": const_type,
        "stack_max": stack_max,
        "opcode_enum": tags["opcodes"],
        "opcodes": ops,
        "examples": build_examples(lines, ops, const_type, stack_max, context),
    }


# ---------------------------------------------------------------------------
# Documents
# ---------------------------------------------------------------------------

def build():
    ids = {name: int(value) for name, value in ID_RE.findall((BLOCKS_DIR / "vm_blocks.h").read_text(encoding="utf-8"))}
    catalog = load_enum_catalog()
    enums = catalog["enums"]
    errors = load_errors()
    sdkconfig = load_sdkconfig()
    blocks = []
    for path in sorted(BLOCKS_DIR.glob("vm_block_*.h")):
        text = path.read_text(encoding="utf-8", errors="ignore").replace("\r\n", "\n")
        for m in BLOCK_RE.finditer(text):
            name = m.group(1)
            context = f"{path.name}: VM_BLK_{name}"
            if name not in ids:
                fail(f"{context}: no #define VM_BLK_{name} in vm_blocks.h")
            tags = parse_tags(m.group("tags"))
            for required in ("title", "category", "activation"):
                if required not in tags:
                    fail(f"{context}: //#vm-block needs @{required}")
            kind, _, activation_text = tags["activation"].partition(" ")
            if kind not in ACTIVATION_KINDS:
                fail(f"{context}: @activation must start with one of {sorted(ACTIVATION_KINDS)}")
            lines = [line[3:].strip() for line in m.group("lines").splitlines()]
            description = next((l[len("block-description"):].strip() for l in lines if l.startswith("block-description")), None)
            if not description:
                fail(f"{context}: needs //@block-description")
            shape = parse_type_macro(text, name, context)
            inputs, outputs = parse_pins(lines, shape["required_in"], context)
            block = {
                "$schema": BLOCK_SCHEMA.relative_to(PROJECT_ROOT).as_posix(),
                "schemaVersion": 1,
                "kind": "vm-block",
                "id": ids[name],
                "name": f"VM_BLK_{name}",
                "title": tags["title"],
                "category": tags["category"],
                "description": description,
                "source_file": path.relative_to(PROJECT_ROOT).as_posix(),
                "activation": {"kind": kind, "description": activation_text.strip() or ACTIVATION_KINDS[kind]},
                "inputs": pin_group(shape["min_in"], inputs, "CONFIG_VM_BLOCK_MAX_IN", sdkconfig),
                "outputs": pin_group(shape["min_q"], outputs, "CONFIG_VM_BLOCK_MAX_OUT", sdkconfig),
                "rules": parse_rules(lines, errors, context),
            }
            if "state" in tags:
                block["state"] = layout_state(tags["state"], enums, context)
                if "state_tail" in tags:
                    block["state"]["tail"] = tags["state_tail"]
            block["min_custom_len"] = block["state"]["size"] if "state" in block else 0
            if "opcodes" in tags:
                block["encoding"] = bytecode_encoding(text, tags, lines, inputs, catalog, context)
            used = sorted({f["enum_ref"] for f in block.get("state", {}).get("fields", []) if "enum_ref" in f} |
                          ({block["encoding"]["opcode_enum"]} if "encoding" in block else set()))
            block["enums"] = {e: enums[e] for e in used}
            blocks.append(block)
    listed = {b["name"] for b in blocks}
    missing = sorted(f"VM_BLK_{n}" for n, v in ids.items() if v != 0 and f"VM_BLK_{n}" not in listed)
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
        return
    schema = json.loads(schema_path.read_text(encoding="utf-8"))
    errors = sorted(jsonschema.Draft202012Validator(schema).iter_errors(document), key=lambda e: list(e.path))
    if errors:
        fail(f"{label} failed schema validation:\n" + "\n".join(f"  - at {'/'.join(map(str, e.path)) or '<root>'}: {e.message}" for e in errors))


def main():
    blocks, index = build()
    if jsonschema is None:
        print("WARNING: jsonschema package not installed - skipping schema validation")
    for block in blocks:
        validate(block, BLOCK_SCHEMA, block["name"])
    validate(index, INDEX_SCHEMA, "index")
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    wanted = {file_name(b) for b in blocks} | {"index.generated.json"}
    for stale in OUT_DIR.glob("*.generated.json"):
        if stale.name not in wanted:
            stale.unlink()  # a block that no longer exists
    for block in blocks:
        (OUT_DIR / file_name(block)).write_text(json.dumps(block, indent=2) + "\n", encoding="utf-8")
    (OUT_DIR / "index.generated.json").write_text(json.dumps(index, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {OUT_DIR.relative_to(PROJECT_ROOT)} ({len(blocks)} blocks + index)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
