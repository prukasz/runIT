"""Generate the VM block catalog the app compiles programs against.

Every block header in components/VM/blocks/ carries, right above its palette
entry macro:

  //#vm-block VM_BLK_<NAME> @title <text> @category <word> [@state <struct>] [@state-tail <text>]
  //@block-description <text>
  //@in <index|*> <name> @title <text> [@description <text>]
  //@out <index|*> <name> @title <text> [@description <text>]
  #define VM_BLOCK_TYPE_<NAME> {.run = ..., .min_in = N, .min_q = N, .required_in = 0x..u, .state_len = ...}

The block id comes from VM_BLK_<NAME> in vm_blocks.h and the shape (minimum
pins, required inputs) from the same VM_BLOCK_TYPE_<NAME> the firmware checks
at load, so the catalog cannot disagree with what the device accepts. The
private-state struct is laid out here (natural C alignment, which every state
struct follows with explicit padding) and cross-checked against its
_Static_assert(sizeof(...) == N). Field comments: text before the first tag is
the description; `@enum-ref <enum>` names a published //#ref-enum;
`@runtime` marks bytes the device owns (the app writes 0).

Usage:
  python data-structures/auto-annotations/vm/generate-vm-blocks.py
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
VM_ROOT = PROJECT_ROOT / "components" / "VM"
BLOCKS_DIR = VM_ROOT / "blocks"
OUT_PATH = PROJECT_ROOT / "data-structures" / "vm" / "vm-blocks.generated.json"
SCHEMA_PATH = PROJECT_ROOT / "data-structures" / "schema" / "vm-blocks.schema.json"

ID_RE = re.compile(r"^#define\s+VM_BLK_(\w+)\s+(\d+)", re.MULTILINE)
BLOCK_RE = re.compile(r"^//#vm-block\s+VM_BLK_(\w+)(?P<tags>[^\n]*)\n(?P<lines>(?://@[^\n]*\n)*)", re.MULTILINE)
TYPE_RE = r"#define\s+VM_BLOCK_TYPE_{name}\s*\\\s*\n\s*\{{(?P<body>[^}}]*)\}}"
TAG_RE = re.compile(r"@([\w-]+)\b")
PIN_RE = re.compile(r"^(?P<dir>in|out)\s+(?P<index>\*|\d+)\s+(?P<name>\w+)(?P<tags>.*)$")
FIELD_RE = re.compile(r"^\s*(?P<type>[A-Za-z_][\w ]*?)\s+(?P<name>\w+)\s*(?:\[(?P<arr>\w*)\])?\s*;\s*(?://\s*(?P<comment>.*))?$")

# (size, alignment) of every type a state struct may use.
TYPES = {
    "uint8_t": (1, 1), "int8_t": (1, 1), "bool": (1, 1), "char": (1, 1),
    "uint16_t": (2, 2), "int16_t": (2, 2),
    "uint32_t": (4, 4), "int32_t": (4, 4), "float": (4, 4),
    "uint64_t": (8, 8), "int64_t": (8, 8), "double": (8, 8),
    "vm_span_t": (4, 2), "vm_edge_val_u": (4, 4), "vm_expr_k_t": (4, 4),
}


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
    return module.scan(module.COMPONENTS_DIR)["enums"]


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
        if "runtime" in tags:
            field["runtime"] = True
        if field["name"].startswith("_"):
            field["padding"] = True
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
        index = m.group("index")
        pin = {"index": "*" if index == "*" else int(index), "name": m.group("name"), "title": tags["title"]}
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


def build():
    ids = {name: int(value) for name, value in ID_RE.findall((BLOCKS_DIR / "vm_blocks.h").read_text(encoding="utf-8"))}
    enums = load_enum_catalog()
    blocks = []
    for path in sorted(BLOCKS_DIR.glob("vm_block_*.h")):
        text = path.read_text(encoding="utf-8", errors="ignore").replace("\r\n", "\n")
        for m in BLOCK_RE.finditer(text):
            name = m.group(1)
            context = f"{path.name}: VM_BLK_{name}"
            if name not in ids:
                fail(f"{context}: no #define VM_BLK_{name} in vm_blocks.h")
            tags = parse_tags(m.group("tags"))
            for required in ("title", "category"):
                if required not in tags:
                    fail(f"{context}: //#vm-block needs @{required}")
            lines = [line[3:].strip() for line in m.group("lines").splitlines()]
            description = next((l[len("block-description"):].strip() for l in lines if l.startswith("block-description")), None)
            if not description:
                fail(f"{context}: needs //@block-description")
            shape = parse_type_macro(text, name, context)
            inputs, outputs = parse_pins(lines, shape["required_in"], context)
            block = {
                "id": ids[name],
                "name": f"VM_BLK_{name}",
                "title": tags["title"],
                "category": tags["category"],
                "description": description,
                "source_file": path.relative_to(PROJECT_ROOT).as_posix(),
                "min_inputs": shape["min_in"],
                "min_outputs": shape["min_q"],
                "inputs": inputs,
                "outputs": outputs,
            }
            if "state" in tags:
                block["state"] = layout_state(tags["state"], enums, context)
                if "state_tail" in tags:
                    block["state"]["tail"] = tags["state_tail"]
            blocks.append(block)
    listed = {b["name"] for b in blocks}
    missing = sorted(f"VM_BLK_{n}" for n, v in ids.items() if v != 0 and f"VM_BLK_{n}" not in listed)
    if missing:
        fail(f"palette ids without a //#vm-block directive: {missing}")
    blocks.sort(key=lambda b: b["id"])
    return {
        "$schema": SCHEMA_PATH.relative_to(PROJECT_ROOT).as_posix(),
        "schemaVersion": 1,
        "kind": "vm-blocks",
        "enums": "data-structures/enums.json",
        "blocks": blocks,
    }


def main():
    document = build()
    if jsonschema is None:
        print("WARNING: jsonschema package not installed - skipping schema validation")
    else:
        schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
        errors = sorted(jsonschema.Draft202012Validator(schema).iter_errors(document), key=lambda e: list(e.path))
        if errors:
            fail("vm-blocks failed schema validation:\n" + "\n".join(f"  - at {'/'.join(map(str, e.path)) or '<root>'}: {e.message}" for e in errors))
    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUT_PATH.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {OUT_PATH.relative_to(PROJECT_ROOT)} ({len(document['blocks'])} blocks)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
