"""Generate the VM program wire format the app builds, sizes and watches programs with.

Sources (grammar: vm-annotations.md "Program wire format"):

  components/VM/core/loader/vm_wire.h
      //#vm-packet <HEADER_symbol> @title .. @when .. [@batch <count type>] [@batch-max <n>] @description ..
      //#vm-wire-struct
      //@tail <name> <type>[<length field>] [@bytes <field>] [@alias ..] [@reference ..] [@none ..] ..
      //@rule <text> @error <ERR_TAG>
      typedef struct __packed { ... } vm_wire_<name>_t;      + _Static_assert(sizeof(...) == N)
      //#vm-wire-union <name> @tag <field> @enum-ref <enum> @description ..
      //@case $<SYMBOL> <struct>
  components/VM/core/sub/vm_sub.c
      //#vm-telemetry-stream <CONFIG stream> @class <CONFIG class> @description ..
      //#vm-telemetry <CONFIG header> @record <struct> @title .. @description ..
  components/VM/**/*.c
      //#vm-arena-align <n> @description ..
      //#vm-arena <item> @per <HEADER_symbol|word> @size <C expression> [@description ..]
  components/VM/**/*.h
      #define NAME value  //@vm-constant @description ..
  components/VM/Kconfig + sdkconfig: every int / hex VM option, as a limit.
  vm_obj.h vm_obj_type_sizes[]: memory width per type; wire width = memory,
  except PTR (VM_OBJ_PTR_WIRE_SIZE).

Every struct layout is computed from its fields and checked against its
_Static_assert; every sizeof() in an arena formula must have one; every
vm_store_alloc() call in the VM core must carry a //#vm-arena line; every
@error must name a real error tag and every @enum-ref a published enum.

Usage:
  python data-structures/auto-annotations/vm/generate-vm-program.py
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
COMPONENTS = PROJECT_ROOT / "components"
VM_ROOT = COMPONENTS / "VM"
WIRE_H = VM_ROOT / "core" / "loader" / "vm_wire.h"
SUB_C = VM_ROOT / "core" / "sub" / "vm_sub.c"
OBJ_H = VM_ROOT / "core" / "obj" / "vm_obj.h"
KCONFIG = VM_ROOT / "Kconfig"
OUT_PATH = PROJECT_ROOT / "data-structures" / "vm" / "vm-program.generated.json"
SCHEMA_PATH = PROJECT_ROOT / "data-structures" / "schema" / "vm-program.schema.json"

_spec = importlib.util.spec_from_file_location("generate_devices", PROJECT_ROOT / "data-structures" / "auto-annotations" / "device" / "generate-devices.py")
device = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = device
_spec.loader.exec_module(device)

C_SIZES = {"uint8_t": 1, "int8_t": 1, "char": 1, "bool": 1, "uint16_t": 2, "int16_t": 2, "uint32_t": 4, "int32_t": 4, "float": 4}
REFERENCES = {"object", "accessor", "block", "block-type"}

STRUCT_RE = re.compile(r"typedef\s+struct\s+__packed\s*\{(?P<body>.*?)\}\s*(?P<name>\w+)\s*;", re.DOTALL)
ASSERT_RE = re.compile(r"_Static_assert\s*\(\s*sizeof\s*\(\s*(?:struct\s+)?(?P<name>[\w*\s]+?)\s*\)\s*==\s*(?P<expr>[^,]+),")
DIRECTIVE_RE = re.compile(r"^\s*//(?P<kind>#vm-packet|#vm-wire-struct|@tail|@rule)\b(?P<rest>.*)$")
UNION_RE = re.compile(r"^//#vm-wire-union\s+(?P<name>\w+)(?P<tags>[^\n]*)\n(?P<cases>(?://@case[^\n]*\n)+)", re.MULTILINE)
CASE_RE = re.compile(r"//@case\s+\$(?P<symbol>\w+)\s+(?P<struct>\w+)")
TAIL_RE = re.compile(r"^\s*(?P<name>\w+)\s+(?P<type>\w+)\[(?P<len>[\w.]+)\](?P<tags>.*)$")
ARENA_RE = re.compile(r"^\s*//#vm-arena\s+(?P<item>[\w-]+)(?P<tags>.*)$", re.MULTILINE)
ALIGN_RE = re.compile(r"^\s*//#vm-arena-align\s+(?P<n>\d+)(?P<tags>.*)$", re.MULTILINE)
CONST_RE = re.compile(r"^#define\s+(?P<name>\w+)\s+(?P<value>[^\s/]+)\s*//@vm-constant(?P<tags>.*)$", re.MULTILINE)
TELEMETRY_RE = re.compile(r"^//#vm-telemetry\s+(?P<header>\w+)(?P<tags>.*)$", re.MULTILINE)
STREAM_RE = re.compile(r"^//#vm-telemetry-stream\s+(?P<stream>\w+)(?P<tags>.*)$", re.MULTILINE)
ERROR_TAG_RE = re.compile(r"X\(\s*(ERR_\w+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,")
TYPE_SIZE_RE = re.compile(r"\[(?P<symbol>VM_OBJ_\w+)\]\s*=\s*sizeof\((?P<ctype>[\w* ]+)\)")


def fail(message):
    raise SystemExit(f"ERROR: {message}")


def tags_of(text):
    return device.parse_tags(text or "")


def rel(path):
    return path.relative_to(PROJECT_ROOT).as_posix()


# ---------------------------------------------------------------------------
# Inputs
# ---------------------------------------------------------------------------

def load_inputs():
    catalog_spec = importlib.util.spec_from_file_location("generate_enums", PROJECT_ROOT / "data-structures" / "auto-annotations" / "enums" / "generate-enums.py")
    enums_mod = importlib.util.module_from_spec(catalog_spec)
    catalog_spec.loader.exec_module(enums_mod)
    catalog = enums_mod.scan(enums_mod.COMPONENTS_DIR)
    # C literal suffixes (4u, 0xFFFFu) resolve like plain numbers.
    defines = {k: re.sub(r"(?<=[0-9A-Fa-f])[uUlL]+$", "", v.strip()) for k, v in device.scan_defines(COMPONENTS).items()}
    for h in (COMPONENTS / "codecs" / "decoders").glob("dec_*.h"):  # packet headers are mixed-case
        for name, value in device.PACKET_HEADER_RE.findall(h.read_text(encoding="utf-8", errors="ignore")):
            defines[f"HEADER_{name}"] = value
    sdkconfig = device.scan_sdkconfig(PROJECT_ROOT / "sdkconfig")
    errors = {}
    for h in COMPONENTS.rglob("*.h"):
        for name, value in ERROR_TAG_RE.findall(h.read_text(encoding="utf-8", errors="ignore")):
            errors[name] = int(value, 16)
    sizes = {}
    for f in list(VM_ROOT.rglob("*.h")) + list(VM_ROOT.rglob("*.c")):
        for m in ASSERT_RE.finditer(f.read_text(encoding="utf-8", errors="ignore")):
            value = resolve(m.group("expr").strip(), defines, sdkconfig, f"{rel(f)}: sizeof({m.group('name')})")
            sizes[" ".join(m.group("name").split()).replace(" *", "*")] = value
    return catalog, defines, sdkconfig, errors, sizes


def resolve(expr, defines, sdkconfig, context):
    """A literal, #define, CONFIG_ symbol or a small sum of them."""
    total = 0
    for term in expr.split("+"):
        term = term.strip().strip("()")
        value, shown = device.resolve_numeric(term, defines, sdkconfig)
        if value is None:
            fail(f"{context}: cannot resolve '{term}' ({shown})")
        total += value
    return total


# ---------------------------------------------------------------------------
# Structs, tails, unions
# ---------------------------------------------------------------------------

def field_entry(f, offset, size, defines, sdkconfig, symbols, catalog, context):
    out = device.field_json(f, defines, sdkconfig, symbols, context)
    out.pop("desc", None)
    out["offset"] = offset
    out["size"] = size
    if f.tags.get("description"):
        out["description"] = f.tags["description"]
    for key in ("reference", "struct_ref"):
        if key in f.tags:
            out[key] = f.tags[key]
    if "reference" in out and out["reference"] not in REFERENCES:
        fail(f"{context}: field '{f.name}': @reference must be one of {sorted(REFERENCES)}")
    if "none" in f.tags:
        out["none"] = resolve(f.tags["none"], defines, sdkconfig, context)
    for key in ("max", "min"):
        if key in f.tags and device.to_int(f.tags[key]) is None:
            out[f"{key}_symbol"] = f.tags[key]
            if out.get(key) is None:
                fail(f"{context}: field '{f.name}': @{key} {f.tags[key]} does not resolve")
    if "enum_ref" in out and out["enum_ref"] not in catalog["enums"]:
        fail(f"{context}: field '{f.name}': @enum-ref {out['enum_ref']} is not a published //#ref-enum")
    return out


def tail_entry(line, field_names, unions, defines, sdkconfig, context):
    m = TAIL_RE.match(line)
    if not m:
        fail(f"{context}: bad //@tail '{line.strip()}' (expected: <name> <type>[<length field>] ...)")
    tags = tags_of(m.group("tags"))
    length = m.group("len")
    if length.split(".")[0] not in field_names:
        fail(f"{context}: tail '{m.group('name')}': length '{length}' is not a field of the record")
    out = {"name": m.group("name"), "type": m.group("type"), "length_from": length}
    if m.group("type") in C_SIZES:
        out["element_size"] = C_SIZES[m.group("type")]
    elif m.group("type") in unions:
        out["element_union"] = m.group("type")
    else:
        fail(f"{context}: tail '{m.group('name')}': unknown element type '{m.group('type')}'")
    if "bytes" in tags:
        if tags["bytes"] not in field_names:
            fail(f"{context}: tail '{m.group('name')}': @bytes '{tags['bytes']}' is not a field of the record")
        out["byte_length_from"] = tags["bytes"]
    for key in ("alias", "description", "encoding", "reference"):
        if key in tags:
            out[key] = tags[key]
    if "reference" in out and out["reference"] not in REFERENCES:
        fail(f"{context}: tail '{m.group('name')}': @reference must be one of {sorted(REFERENCES)}")
    if "none" in tags:
        out["none"] = resolve(tags["none"], defines, sdkconfig, context)
    return out


def parse_wire(catalog, defines, sdkconfig, errors, sizes):
    text = WIRE_H.read_text(encoding="utf-8")
    symbols = catalog["symbols"]
    unions = {}
    for m in UNION_RE.finditer(text):
        tags = tags_of(m.group("tags"))
        unions[m.group("name")] = {"tags": tags, "cases": CASE_RE.findall(m.group("cases"))}

    structs, packets = {}, []
    lines = text.splitlines(keepends=True)
    offsets = [0]
    for line in lines:
        offsets.append(offsets[-1] + len(line))
    for sm in STRUCT_RE.finditer(text):
        name = sm.group("name")
        context = f"{rel(WIRE_H)}: {name}"
        # Directive lines directly above the struct.
        start_line = next(i for i in range(len(lines)) if offsets[i + 1] > sm.start())
        block = []
        i = start_line - 1
        while i >= 0 and DIRECTIVE_RE.match(lines[i]):
            block.insert(0, lines[i])
            i -= 1
        heads = [b for b in block if re.match(r"^\s*//#vm-(packet|wire-struct)\b", b)]
        if not heads:
            continue
        if len(heads) != 1 or not block[0].lstrip().startswith("//#"):
            fail(f"{context}: one //#vm-packet or //#vm-wire-struct line must start the directive block")

        fields = device.parse_struct_fields(sm.group("body"), defines, sdkconfig)
        offset, entries, order = 0, {}, []
        for f in fields:
            if f.type not in C_SIZES:
                fail(f"{context}: field '{f.name}' has type '{f.type}' (allowed: {sorted(C_SIZES)})")
            size = C_SIZES[f.type] * (f.array_len or 1)
            entries[f.name] = field_entry(f, offset, size, defines, sdkconfig, symbols, catalog, context)
            order.append(f.name)
            offset += size
        if name not in sizes:
            fail(f"{context}: needs _Static_assert(sizeof({name}) == N)")
        if sizes[name] != offset:
            fail(f"{context}: fields add up to {offset} bytes, _Static_assert says {sizes[name]}")
        tails = [tail_entry(b.split("//@tail", 1)[1], order, unions, defines, sdkconfig, context) for b in block if "//@tail" in b]
        structs[name] = {"name": name, "size": offset, "field_order": order, "fields": entries, **({"tails": tails} if tails else {})}

        head = heads[0]
        if "#vm-packet" not in head:
            continue
        pm = re.match(r"^\s*//#vm-packet\s+(?P<header>\w+)(?P<tags>.*)$", head)
        tags = tags_of(pm.group("tags"))
        for required in ("title", "when", "description"):
            if not tags.get(required):
                fail(f"{context}: //#vm-packet needs @{required}")
        rules = []
        for b in block:
            if "//@rule" not in b:
                continue
            rtags = tags_of(b.split("//@rule", 1)[1])
            err = rtags.get("error")
            if not rtags.get("desc") or not err:
                fail(f"{context}: //@rule needs text and @error")
            if err not in errors:
                fail(f"{context}: //@rule @error {err} is not an error tag")
            rules.append({"rule": rtags["desc"], "error": err, "error_tag": errors[err]})
        packet = {
            "packet_header": f"0x{resolve(pm.group('header'), defines, sdkconfig, context):02X}",
            "symbol": pm.group("header"),
            "title": tags["title"],
            "description": tags["description"],
            "when": tags["when"],
        }
        if "batch" in tags:
            if tags["batch"] not in C_SIZES:
                fail(f"{context}: @batch must be a C integer type")
            packet["batch"] = {"count_type": tags["batch"], "max": 255}
            if "batch_max" in tags:
                packet["batch"]["max"] = resolve(tags["batch_max"], defines, sdkconfig, context)
                packet["batch"]["max_symbol"] = tags["batch_max"]
        packet["record"] = name
        if rules:
            packet["rules"] = rules
        packets.append(packet)

    union_out = {}
    for uname, u in unions.items():
        tag_field = u["tags"].get("tag")
        enum = u["tags"].get("enum_ref")
        if enum not in catalog["enums"]:
            fail(f"{rel(WIRE_H)}: union {uname}: @enum-ref {enum} is not a published enum")
        cases = []
        for symbol, struct in u["cases"]:
            if symbol not in symbols or symbols[symbol]["enum"] != enum:
                fail(f"{rel(WIRE_H)}: union {uname}: ${symbol} is not a member of {enum}")
            if struct not in structs:
                fail(f"{rel(WIRE_H)}: union {uname}: case struct {struct} has no //#vm-wire-struct")
            if structs[struct]["field_order"][0] != tag_field:
                fail(f"{rel(WIRE_H)}: union {uname}: {struct} must start with the tag field '{tag_field}'")
            cases.append({"symbol": symbol, "value": symbols[symbol]["value"], "record": structs[struct]})
        union_out[uname] = {"tag": tag_field, "tag_type": structs[u["cases"][0][1]]["fields"][tag_field]["type"], "enum_ref": enum,
                            "description": u["tags"].get("description", ""), "cases": cases}
    for packet in packets:
        packet["record"] = structs[packet["record"]]
    return structs, packets, union_out


# ---------------------------------------------------------------------------
# Types, constants, limits, arena, telemetry
# ---------------------------------------------------------------------------

def parse_types(catalog, defines, sdkconfig, sizes):
    text = OBJ_H.read_text(encoding="utf-8")
    table = text[text.index("vm_obj_type_sizes[]"):]
    table = table[: table.index("};")]
    ptr_wire = resolve("VM_OBJ_PTR_WIRE_SIZE", defines, sdkconfig, rel(OBJ_H))
    members = {m["name"]: m for m in catalog["enums"]["vm_obj_t_e"]["members"]}
    out = []
    for m in TYPE_SIZE_RE.finditer(table):
        symbol, ctype = m.group("symbol"), " ".join(m.group("ctype").split())
        if symbol == "VM_OBJ_NONE":
            continue
        if symbol not in members:
            fail(f"{rel(OBJ_H)}: {symbol} is not a vm_obj_t_e member")
        memory = sizes.get(ctype.replace(" *", "*"), C_SIZES.get(ctype))
        if memory is None:
            fail(f"{rel(OBJ_H)}: no size for sizeof({ctype}) (add a _Static_assert)")
        is_ptr = symbol == "VM_OBJ_PTR"
        out.append({
            "symbol": symbol,
            "value": members[symbol]["value"],
            "alias": members[symbol]["alias"],
            "memory_width": memory,
            "wire_width": ptr_wire if is_ptr else memory,
            "wire_type": "uint16_t" if is_ptr else ctype,
            **({"wire_note": "child object ID (VM_OBJ_ID_NONE = unlinked)"} if is_ptr else {}),
        })
    if not out:
        fail(f"{rel(OBJ_H)}: vm_obj_type_sizes[] not found")
    return out


def parse_constants(defines, sdkconfig):
    out = {}
    for h in sorted(VM_ROOT.rglob("*.h")):
        for m in CONST_RE.finditer(h.read_text(encoding="utf-8", errors="ignore")):
            tags = tags_of(m.group("tags"))
            out[m.group("name")] = {"value": resolve(m.group("name"), defines, sdkconfig, rel(h)), "description": tags.get("description", ""), "source_file": rel(h)}
    return out


def parse_limits(sdkconfig):
    out = {}
    blocks = re.split(r"^\s*config\s+", KCONFIG.read_text(encoding="utf-8"), flags=re.MULTILINE)[1:]
    for block in blocks:
        name = block.split()[0]
        kind = re.search(r"^\s*(int|hex|bool|string)\s+\"(?P<prompt>[^\"]*)\"", block, re.MULTILINE)
        if not kind or kind.group(1) not in ("int", "hex"):
            continue
        key = f"CONFIG_{name}"
        if key not in sdkconfig:
            fail(f"{rel(KCONFIG)}: {key} missing from sdkconfig (run idf.py reconfigure)")
        help_text = ""
        hm = re.search(r"^(?P<indent>[ \t]*)help[ \t]*$", block, re.MULTILINE)
        if hm:
            # Help text is everything indented deeper than the `help` keyword.
            text_lines = []
            for line in block[hm.end():].splitlines()[1:]:
                if line.strip() and len(line) - len(line.lstrip()) <= len(hm.group("indent")):
                    break
                text_lines.append(line.strip())
            help_text = " ".join(line for line in text_lines if line)
        out[key] = {"value": sdkconfig[key], "title": kind.group("prompt"), **({"help": help_text} if help_text else {})}
    return out


def expand_sizes(expr, sizes, context):
    def repl(m):
        name = " ".join(m.group(1).split()).replace(" *", "*")
        if name not in sizes:
            fail(f"{context}: sizeof({name}) has no _Static_assert")
        return str(sizes[name])
    return re.sub(r"sizeof\(\s*([\w* ]+?)\s*\)", repl, expr)


def parse_arena(defines, sdkconfig, sizes):
    align, items, unannotated = None, [], []
    for c in sorted(VM_ROOT.rglob("*.c")):
        text = c.read_text(encoding="utf-8", errors="ignore")
        for m in ALIGN_RE.finditer(text):
            align = {"alignment": int(m.group("n")), "description": tags_of(m.group("tags")).get("description", "")}
        for m in ARENA_RE.finditer(text):
            tags = tags_of(m.group("tags"))
            context = f"{rel(c)}: //#vm-arena {m.group('item')}"
            if not tags.get("size") or not tags.get("per"):
                fail(f"{context}: needs @per and @size")
            per = tags["per"]
            entry = {"item": m.group("item"), "per": per}
            if per.startswith("HEADER_"):
                entry["per_packet"] = f"0x{resolve(per, defines, sdkconfig, context):02X}"
            entry["size"] = tags["size"]
            entry["size_bytes"] = expand_sizes(tags["size"], sizes, context)
            if tags.get("description"):
                entry["description"] = tags["description"]
            items.append(entry)
        # Every arena allocation outside the store itself carries its formula.
        lines = text.splitlines()
        for i, line in enumerate(lines):
            if "vm_store_alloc(" in line and not c.name.startswith("vm_store") and not line.lstrip().startswith(("//", "*")):
                if not any("//#vm-arena " in lines[j] for j in range(max(0, i - 6), i)):
                    unannotated.append(f"{rel(c)}:{i + 1}")
    if unannotated:
        fail("vm_store_alloc() without a //#vm-arena line right above it: " + ", ".join(unannotated))
    if not align:
        fail("no //#vm-arena-align found")
    # Load order: registries at open, then per packet; a name step goes with its accessor.
    rank = {"open": "0x41", "VM_IDX_NAME": "0x44~"}
    items.sort(key=lambda e: e.get("per_packet") or rank.get(e["per"], "~"))
    return {**align, "total_size": "sum over every allocation of its size rounded up to the alignment", "allocations": items}


def parse_telemetry(structs, defines, sdkconfig):
    text = SUB_C.read_text(encoding="utf-8")
    sm = STREAM_RE.search(text)
    if not sm:
        fail(f"{rel(SUB_C)}: no //#vm-telemetry-stream")
    stags = tags_of(sm.group("tags"))
    frames = []
    for m in TELEMETRY_RE.finditer(text):
        tags = tags_of(m.group("tags"))
        context = f"{rel(SUB_C)}: //#vm-telemetry {m.group('header')}"
        if tags.get("record") not in structs:
            fail(f"{context}: @record must name a vm_wire.h struct")
        frames.append({
            "packet_header": f"0x{resolve(m.group('header'), defines, sdkconfig, context):02X}",
            "symbol": m.group("header"),
            "title": tags.get("title", ""),
            "description": tags.get("description", ""),
            "batch": {"count_type": "uint8_t", "max": 255},
            "record": structs[tags["record"]],
        })
    return {
        "stream": f"0x{resolve(sm.group('stream'), defines, sdkconfig, rel(SUB_C)):02X}",
        "stream_symbol": sm.group("stream"),
        "class_header": f"0x{resolve(stags['class'], defines, sdkconfig, rel(SUB_C)):02X}",
        "description": stags.get("description", ""),
        "frames": frames,
    }


def build():
    catalog, defines, sdkconfig, errors, sizes = load_inputs()
    structs, packets, unions = parse_wire(catalog, defines, sdkconfig, errors, sizes)
    arena = parse_arena(defines, sdkconfig, sizes)
    class_key = "CONFIG_RX_PACKET_CLASS_VM_LOADER"
    if class_key not in sdkconfig:
        fail(f"{class_key} missing from sdkconfig")
    return {
        "$schema": SCHEMA_PATH.relative_to(PROJECT_ROOT).as_posix(),
        "schemaVersion": 1,
        "kind": "vm-program",
        "class_header": f"0x{sdkconfig[class_key]:02X}",
        "request_prefix": ["seq"],
        "framing": "One packet per inbound frame: [seq][class][packet][body]. A frame carries at most the link's payload (BLE: MTU - 3); split batch records over several packets.",
        "response": "Every packet is answered on the response stream (contracts.generated.json response_stream) with status only: no OK data.",
        "types": parse_types(catalog, defines, sdkconfig, sizes),
        "constants": parse_constants(defines, sdkconfig),
        "limits": parse_limits(sdkconfig),
        "sizes": {name: sizes[name] for name in sorted(sizes) if any(f"sizeof({name})" in a["size"] for a in arena["allocations"])},
        "arena": arena,
        "packets": packets,
        "unions": unions,
        "telemetry": parse_telemetry(structs, defines, sdkconfig),
    }


def main():
    document = build()
    if jsonschema is None:
        print("WARNING: jsonschema package not installed - skipping schema validation")
    else:
        schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
        errors = sorted(jsonschema.Draft202012Validator(schema).iter_errors(document), key=lambda e: list(e.path))
        if errors:
            fail("vm-program failed schema validation:\n" + "\n".join(f"  - at {'/'.join(map(str, e.path)) or '<root>'}: {e.message}" for e in errors))
    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUT_PATH.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {OUT_PATH.relative_to(PROJECT_ROOT)} ({len(document['packets'])} packets, {len(document['telemetry']['frames'])} telemetry frames, "
          f"{len(document['limits'])} limits, {len(document['arena']['allocations'])} arena allocations)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
