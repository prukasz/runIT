"""The command catalog: every command the board answers and every device contract, as one JSON for the app's codec.

Grammar and rules: contract-annotations.md. In short, two kinds of commands, both described next to the C that implements them:

  * A router module's ops: the table `static const router_entry_t k_ops[] = {{OP_X, op_x}, ...};` of the module that
    `router_register(ROUTER_KEY_<X>, <module>)` binds. Above each row `//@op <OP_SYMBOL> @alias <name> @description <text> [@returns ...]`.
    The op's arguments are the struct the handler macro names (`ROUTER_OP(op_x, args_t)`, `ROUTER_OP_TAIL`, `ROUTER_OP_NOARGS`).
  * A device contract: an X-macro table `#define SYS_IO_CONTRACT(X) X(NAME, 0xNN, args_t) /* @alias ... @description ... */` that
    generates the op ids `IO_OP_<NAME>`, and an events table `X(NAME, 0xNN, flags) /* ... */`.

Wire structs are packed little-endian structs; their fields carry `@alias` (the name a user sees), `@unit`, `@min`, `@max`,
`@enum-ref`, `@choices`, `@id` ... in the trailing comment. Nothing a user sees is derived from a C identifier: a field, op or event
without an `@alias` is an error. A command and its answer use one codec: the answer of an op is described the same way as an
argument struct.

Output: data-structures/contracts/contracts.generated.json (LF endings; generated, do not edit).

Usage:
  python data-structures/auto-annotations/contracts/generate-contracts.py [--check]
"""
import importlib.util
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
COMPONENTS_DIR = ROOT / "components"
SDKCONFIG = ROOT / "sdkconfig"
OUT = ROOT / "data-structures" / "contracts" / "contracts.generated.json"
ENUMS_PATH = ROOT / "data-structures" / "auto-annotations" / "enums" / "generate-enums.py"
SCHEMA_VERSION = 2
SKIP_DIRS = {"build", "legacy"}

SCALARS = {  # C type -> (wire type, bytes)
    "uint8_t": ("u8", 1), "int8_t": ("i8", 1), "bool": ("u8", 1), "char": ("char", 1),
    "uint16_t": ("u16", 2), "int16_t": ("i16", 2),
    "uint32_t": ("u32", 4), "int32_t": ("i32", 4), "float": ("f32", 4),
    "uint64_t": ("u64", 8), "int64_t": ("i64", 8),
}
WIRE_TYPES = {"u8": 1, "i8": 1, "u16": 2, "i16": 2, "u32": 4, "i32": 4, "u64": 8, "i64": 8, "f32": 4}
ID_KINDS = {"device": "A device id (u16).", "pin": "An index of a pin or channel of a device (the alias says which).",
            "channel": "A channel of an event.", "event": "An event id.", "action": "A record id of the action store (u32)."}

FIELD_TAGS_BASE = {"alias", "description", "unit", "min", "max", "enum-ref", "choices", "id", "device-field", "default", "any"}
FIELD_TAGS = set(FIELD_TAGS_BASE)
OP_TAGS = {"alias", "description", "args", "returns", "returns-alias", "returns-description", "unit", "choices", "tail", "tail-alias",
           "tail-description", "returns-tail", "returns-tail-alias", "returns-tail-description", "urgent", "payload"}
EVENT_TAGS = {"alias", "description", "value", "unit"}
FLAG_TAGS = {"urgent"}
TAG_RE = re.compile(r"(?:^|(?<=\s))@([a-z][a-z0-9-]*)(?=\s|$)")
FIELD_RE = re.compile(r"^\s*(?P<type>[A-Za-z_][\w ]*?)\s+(?P<name>\w+)\s*(?:\[(?P<arr>[^\]]*)\])?\s*;\s*(?://\s*(?P<comment>.*))?$")
STRUCT_RE = re.compile(r"typedef\s+struct\s*(?:__attribute__\s*\(\(\s*(?:__)?packed(?:__)?\s*\)\)|__packed)?\s*\w*\s*\{(?P<body>[^{}]*)\}\s*(?P<name>\w+)\s*;")
ENTRY_RE = re.compile(r"\{\s*(\w+)\s*,\s*(\w+)\s*\}")
REGISTER_RE = re.compile(r"router_register\(\s*(ROUTER_KEY_\w+)\s*,\s*(\w+)\s*\)")
HANDLER_RE = re.compile(r"ROUTER_OP\(\s*(\w+)\s*,\s*(\w+)\s*\)|ROUTER_OP_TAIL\(\s*(\w+)\s*,\s*(\w+)\s*\)|ROUTER_OP_NOARGS\(\s*(\w+)\s*\)")
ROW_RE = re.compile(r"X\(\s*(\w+)\s*,\s*(0x[0-9a-fA-F]+|\d+)\s*,\s*(\w+)\s*\)\s*(?:/\*(?P<comment>.*?)\*/)?", re.DOTALL)
CONFIG_RE = re.compile(r"^(CONFIG_\w+)=(0x[0-9a-fA-F]+|-?\d+)\s*$", re.MULTILINE)


def fail(message):
    raise SystemExit(f"ERROR: {message}")


def rel(path):
    return path.relative_to(ROOT).as_posix()


_CACHE = {}
KEEP_TAGS = False  # the device generator keeps each field's raw tags (`_tags`) to read the ones it adds


def read(path):
    key = str(path)
    if key not in _CACHE:
        _CACHE[key] = path.read_text(encoding="utf-8", errors="ignore").replace("\r\n", "\n")
    return _CACHE[key]


def source_files(suffixes=(".h", ".c")):
    if "files" not in _CACHE:
        _CACHE["files"] = sorted(p for p in COMPONENTS_DIR.rglob("*") if p.suffix in (".h", ".c") and not SKIP_DIRS & set(p.relative_to(COMPONENTS_DIR).parts))
    return [p for p in _CACHE["files"] if p.suffix in suffixes]


def enums_module():
    if "enums" not in _CACHE:
        spec = importlib.util.spec_from_file_location("generate_enums", ENUMS_PATH)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        module.ROOT, module.COMPONENTS_DIR, module.SDKCONFIG = ROOT, COMPONENTS_DIR, SDKCONFIG
        _CACHE["enums"] = module
    return _CACHE["enums"]


def enum_catalog():
    if "catalog" not in _CACHE:
        _CACHE["catalog"] = enums_module().scan()[0]
    return _CACHE["catalog"]


def parse_tags(text, allowed, context):
    """(lead, tags): the text before the first tag and `{tag: value}`; an unknown or repeated tag fails."""
    matches = list(TAG_RE.finditer(text))
    lead = (text[:matches[0].start()] if matches else text).strip()
    tags = {}
    for i, m in enumerate(matches):
        name = m.group(1)
        if name not in allowed:
            fail(f"{context}: unknown tag @{name} (this takes: {', '.join('@' + t for t in sorted(allowed))})")
        if name in tags:
            fail(f"{context}: @{name} given twice")
        end = matches[i + 1].start() if i + 1 < len(matches) else len(text)
        value = text[m.end():end].strip()
        if name in FLAG_TAGS and value:
            fail(f"{context}: @{name} takes no value")
        tags[name] = value
    return lead, tags


def safe_int(expr, context):
    text = re.sub(r"(?<=[0-9a-fA-F])[uUlL]+\b", "", expr.strip())
    if not text or not re.fullmatch(r"[0-9a-fA-FxX<>|&+()\s*/-]+", text):
        fail(f"{context}: cannot read `{expr.strip()}` as an integer constant")
    try:
        return int(eval(text, {"__builtins__": {}}, {}))  # noqa: S307 - only numbers and operators survive the check above
    except Exception:
        fail(f"{context}: cannot read `{expr.strip()}` as an integer constant")


def defines():
    if "defines" not in _CACHE:
        found = {}
        for path in source_files():
            for line in read(path).split("\n"):
                m = re.match(r"^#define\s+(\w+)\s+([^\n/]+?)\s*(?://.*|/\*.*)?$", line)
                if m and "(" not in m.group(1):
                    found.setdefault(m.group(1), m.group(2))
            # the members of an anonymous `enum { NAME = n, ... };` are constants too
            for body in re.findall(r"\benum\s*\{([^{}]*)\}\s*;", read(path)):
                for name, value in re.findall(r"^\s*(\w+)\s*=\s*([^,/\n]+?)\s*,?\s*(?://.*)?$", body, re.MULTILINE):
                    found.setdefault(name, value)
        _CACHE["defines"] = found
    return _CACHE["defines"]


def sdkconfig():
    if "sdk" not in _CACHE:
        if not SDKCONFIG.exists():
            fail(f"{rel(SDKCONFIG)} not found: configure the firmware once (idf.py reconfigure)")
        _CACHE["sdk"] = {n: int(v, 0) for n, v in CONFIG_RE.findall(read(SDKCONFIG))}
    return _CACHE["sdk"]


def eval_c(expr, context, seen=()):
    def word(m):
        name = m.group(1)
        if name in sdkconfig():
            return str(sdkconfig()[name])
        if name in seen:
            fail(f"{context}: {name} refers to itself")
        if name in defines():
            return str(eval_c(defines()[name], context, seen + (name,)))
        fail(f"{context}: `{name}` is neither a #define nor a CONFIG_ option")
    return safe_int(re.sub(r"\b([A-Za-z_]\w*)\b", word, expr), context)


# ---------------------------------------------------------------------------
# Wire structs
# ---------------------------------------------------------------------------

def find_struct(name, near=()):
    """(body, path) of `typedef struct {...} name;`, looking in the `near` files first, then every source."""
    for path in [*near, *source_files()]:
        for m in STRUCT_RE.finditer(read(path)):
            if m.group("name") == name:
                return m.group("body"), path
    fail(f"wire struct {name} not found under components/")


def parse_choices(text, context):
    choices = []
    for item in text.replace(",", " ").split():
        m = re.fullmatch(r"(-?\d+)=(.+)", item)
        if not m:
            fail(f"{context}: @choices is `<value>=<Name>` items separated by spaces or commas (`_` is a space inside a name); got `{item}`")
        choices.append({"value": int(m.group(1)), "alias": m.group(2).replace("_", " ")})
    return choices


def field_meta(tags, context):
    meta = {}
    if not tags.get("alias"):
        fail(f"{context}: needs `@alias <the name a user sees>`")
    meta["alias"] = tags["alias"]
    if tags.get("description"):
        meta["description"] = tags["description"]
    for key in ("min", "max"):
        if key in tags:
            meta[key] = eval_c(tags[key], context)  # a number, a #define or a CONFIG_ option
    if "default" in tags:
        meta["default"] = tags["default"]
    if tags.get("unit"):
        meta["unit"] = tags["unit"]
    if "enum-ref" in tags:
        if tags["enum-ref"] not in enum_catalog():
            fail(f"{context}: @enum-ref {tags['enum-ref']} is not a published //#ref-enum")
        meta["enum_ref"] = tags["enum-ref"]
    if "choices" in tags:
        meta["choices"] = parse_choices(tags["choices"], context)
    if "id" in tags:
        if tags["id"] not in ID_KINDS:
            fail(f"{context}: @id must be one of {', '.join(ID_KINDS)}")
        meta["id_kind"] = tags["id"]
    if "device-field" in tags:
        meta["device_field"] = tags["device-field"]
    if "any" in tags:
        if not re.fullmatch(r"\d+", tags["any"]) or int(tags["any"]) > 0xFFFF:
            fail(f"{context}: @any needs the wildcard value (0 to 65535)")
        meta["any_value"] = int(tags["any"])
    return meta


def struct_fields(name, near, context, stack=()):
    """{struct, size, fields[]} of a packed wire struct: offsets are sequential; a nested struct is expanded in place."""
    if name in stack:
        fail(f"{context}: struct {name} contains itself")
    body, path = find_struct(name, near)
    where = f"{context}: {name}"
    lines, declarations = body.split("\n"), []
    for line in lines:
        if not line.strip():
            continue
        if line.strip().startswith("//"):
            if declarations:
                declarations[-1][1].append(line.strip()[2:].strip())
            continue
        declarations.append((line, []))
    fields, offset = [], 0
    for line, continuation in declarations:
        m = FIELD_RE.match(line)
        if not m:
            fail(f"{where}: cannot parse `{line.strip()}`")
        c_type = " ".join(m.group("type").split())
        ctx = f"{where}.{m.group('name')}"
        comment = " ".join([m.group("comment") or "", *continuation]).strip()
        description, tags = parse_tags(comment, FIELD_TAGS | {"description"}, ctx)
        if description and "description" not in tags:
            tags["description"] = description
        field = {"name": m.group("name"), "offset": offset}
        arr = m.group("arr")
        count = None
        if arr is not None:
            if arr == "":
                fail(f"{ctx}: a wire struct has no flexible array (use the op's @tail)")
            count = eval_c(arr, ctx)
        if c_type in SCALARS:
            wire, size = SCALARS[c_type]
            field.update(type=wire, size=size * (count or 1))
            if count is not None:
                field["array_len"] = count
        elif c_type.endswith("_e"):  # an enum field on the wire: its storage is declared by the comment (`@enum-ref`) and a uint8_t here
            fail(f"{ctx}: store an enum on the wire as uint8_t / uint16_t with `@enum-ref {c_type}`")
        else:
            inner = struct_fields(c_type, [path, *near], ctx, (*stack, name))
            field.update(type="struct", struct=c_type, fields=inner["fields"], size=inner["size"] * (count or 1))
            if count is not None:
                field["array_len"] = count
        if not m.group("name").startswith("_"):
            field.update(field_meta({k: v for k, v in tags.items() if k in FIELD_TAGS_BASE}, ctx))
            if KEEP_TAGS:
                field["_tags"] = dict(tags)
        else:
            field["padding"] = True
        offset += field["size"]
        fields.append(field)
    return {"struct": name, "source_file": rel(path), "size": offset, "fields": fields}


def struct_check(text, name, size, context):
    m = re.search(r"_Static_assert\(\s*sizeof\(\s*" + re.escape(name) + r"\s*\)\s*==\s*(\d+)", text)
    if m and int(m.group(1)) != size:
        fail(f"{context}: {name} computes to {size} bytes, _Static_assert says {m.group(1)}")


# ---------------------------------------------------------------------------
# Value / result / tail types
# ---------------------------------------------------------------------------

def type_spec(spec, tags, prefix, near, context):
    """A `@returns` / `@tail` type as the json carries it: a scalar, `T[]`, `string`, `bytes` or a struct name."""
    spec = spec.strip()
    alias = tags.get(f"{prefix}-alias")
    description = tags.get(f"{prefix}-description")
    out = {}
    array = spec.endswith("[]")
    base = spec[:-2] if array else spec
    if base in WIRE_TYPES:
        out["type"] = base
    elif base in ("string", "bytes"):
        out["type"] = base
        if array:
            fail(f"{context}: `{spec}`: {base} is already a sequence")
    else:
        inner = struct_fields(base, near, context)
        out.update(type="struct", struct=base, size=inner["size"], fields=inner["fields"])
    if array:
        out["array"] = True
    if prefix == "returns" and "type" in out and out["type"] != "struct":
        if not alias:
            fail(f"{context}: @returns {spec} needs `@returns-alias <the name of the value>`")
        out["alias"] = alias
        if tags.get("unit"):
            out["unit"] = tags["unit"]
        if "choices" in tags:
            out["choices"] = parse_choices(tags["choices"], context)
    elif out.get("type") != "struct":
        if not alias:
            fail(f"{context}: @{prefix} {spec} needs `@{prefix}-alias <name>`")
        out["alias"] = alias
    if description:
        out["description"] = description
    return out


# ---------------------------------------------------------------------------
# Router ops
# ---------------------------------------------------------------------------

def router_modules():
    """{ROUTER_KEY symbol: module function name}, from every router_register(KEY, fn)."""
    found = {}
    for path in source_files((".c",)):
        for key, fn in REGISTER_RE.findall(read(path)):
            found[key] = fn
    return found


def module_file(fn):
    for path in source_files((".c",)):
        if re.search(r"ROUTER_MODULE\(\s*(?:static\s+)?" + re.escape(fn) + r"\s*,", read(path)) or re.search(r"\berr_h\s+" + re.escape(fn) + r"\s*\(", read(path)):
            return path
    return None


def table_rows(text, context):
    """Rows of `static const router_entry_t k_ops[] = {...};` with the `//@op` lines above each: [(op symbol, handler, [lines])]."""
    m = re.search(r"static\s+const\s+router_entry_t\s+k_ops\[\]\s*=\s*\{", text)
    if not m:
        return None
    depth, end = 1, m.end()
    while depth and end < len(text):
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    rows, pending = [], []
    for line in text[m.end():end - 1].split("\n"):
        stripped = line.strip()
        if stripped.startswith("//@op"):
            pending.append(stripped)
            continue
        if stripped.startswith("//") or not stripped:
            continue
        entries = ENTRY_RE.findall(re.sub(r"//.*$", "", line))
        for symbol, handler in entries:
            rows.append((symbol, handler, pending))
            pending = []
    return rows


def parse_op(symbol, handler, pending, path, key_alias, near, ids, handlers):
    where = f"{rel(path)}: op {symbol}"
    head = [p for p in pending if re.match(r"^//@op\s", p)]
    if len(head) != 1:
        fail(f"{where}: needs one `//@op {symbol} @alias <name> @description <text>` line above its row")
    lead, tags = parse_tags(head[0][len("//@op"):], OP_TAGS, where)
    if lead != symbol:
        fail(f"{where}: the `//@op` line above names `{lead}`, the row is {symbol}")
    if not tags.get("alias") or not tags.get("description"):
        fail(f"{where}: needs @alias and @description")
    if symbol not in ids:
        fail(f"{where}: no `#define {symbol} <n>`")
    op = {"id": ids[symbol], "symbol": symbol, "alias": tags["alias"], "description": tags["description"]}
    if "urgent" in tags:
        op["urgent"] = True
    struct_name, kind = tags.get("args"), None
    macro = handlers.get(handler)
    if macro:
        kind, derived = macro
        if struct_name and derived and struct_name != derived:
            fail(f"{where}: @args {struct_name}, the handler {handler} takes {derived}")
        struct_name = derived or struct_name
    if struct_name and struct_name != "none":
        args = struct_fields(struct_name, near, where)
        struct_check(read(near[0]), struct_name, args["size"], where)
        op["args"] = args
    if "tail" in tags:
        op["tail"] = type_spec(tags["tail"], tags, "tail", near, where)
    elif kind == "tail":
        fail(f"{where}: the handler {handler} takes a tail: describe it with `@tail <type> @tail-alias <name>`")
    if "returns" in tags:
        op["returns"] = type_spec(tags["returns"], tags, "returns", near, where)
    if "returns-tail" in tags:
        op["returns_tail"] = type_spec(tags["returns-tail"], tags, "returns-tail", near, where)
    if "payload" in tags:
        op["payload"] = tags["payload"]
    return op


def build_routers():
    enums = enum_catalog()
    keys = {m["name"]: m for m in enums["router_key"]["members"]}
    modules = router_modules()
    # the sys router is registered by the router itself
    if "ROUTER_KEY_SYS" not in modules:
        for path in source_files((".c",)):
            m = re.search(r"ROUTER_MODULE\(\s*(\w+)\s*,\s*k_ops\s*\)", read(path)) if path.name == "router_sys.c" else None
            if m:
                modules["ROUTER_KEY_SYS"] = m.group(1)
    routers = []
    for symbol, key in sorted(keys.items(), key=lambda kv: kv[1]["value"]):
        entry = {"key": key["value"], "symbol": symbol, "alias": key["alias"], "description": key.get("description", "")}
        fn = modules.get(symbol)
        if fn is None:
            entry["router"] = None  # a key with no router (a frame shape only)
            routers.append(entry)
            continue
        path = module_file(fn)
        if path is None:
            fail(f"router {fn} ({symbol}) is registered but no source defines it")
        text = read(path)
        entry["module"] = fn
        entry["source_file"] = rel(path)
        rows = table_rows(text, rel(path))
        if rows is None:
            m = re.search(r"^//@dispatch\s+(\w+)\s+(.+)$", text, re.MULTILINE)
            if not m:
                fail(f"{rel(path)}: {fn} has no `static const router_entry_t k_ops[]` table: say how it dispatches with `//@dispatch <kind> <layout>`")
            entry["dispatch"] = {"kind": m.group(1), "layout": m.group(2).strip()}
            routers.append(entry)
            continue
        near = [path, *sorted(path.parent.glob("*.h"))]
        local_defines = {n: v for n, v in re.findall(r"^#define\s+(\w+)\s+([^\n/]+?)\s*(?://.*)?$", "\n".join(read(p) for p in near), re.MULTILINE)}
        ids = {}
        for sym in {s for s, _, _ in rows}:
            if sym in local_defines:
                ids[sym] = eval_c(local_defines[sym], f"{rel(path)}: {sym}")
            elif sym in defines():
                ids[sym] = eval_c(defines()[sym], f"{rel(path)}: {sym}")
        handlers = {}
        for m in HANDLER_RE.finditer(text):
            if m.group(1):
                handlers[m.group(1)] = ("fixed", m.group(2))
            elif m.group(3):
                handlers[m.group(3)] = ("tail", m.group(4))
            else:
                handlers[m.group(5)] = ("none", None)
        entry["ops"] = [parse_op(s, h, p, path, key["alias"], near, ids, handlers) for s, h, p in rows]
        values = [o["id"] for o in entry["ops"]]
        if len(values) != len(set(values)):
            fail(f"{rel(path)}: two ops share an id")
        routers.append(entry)
    return routers


# ---------------------------------------------------------------------------
# Device contracts
# ---------------------------------------------------------------------------

def joined(text):
    """The text with every backslash-continued line joined to the next: a macro is one logical line."""
    return re.sub(r"\\[ \t]*\n[ \t]*", " ", text)


def macro_body(text, name):
    """The text of `#define name(X)` up to the end of its logical line."""
    m = re.search(r"^#define\s+" + re.escape(name) + r"\(X\)(?P<body>[^\n]*)", joined(text), re.MULTILINE)
    return m.group("body") if m else None


def contract_tables():
    """[(table macro, id macro prefix, contract symbol, header path, kind)] for every `enum { TABLE(PFX__ID) }` of a device contract."""
    found = []
    for path in source_files((".h",)):
        text = joined(read(path))
        for id_macro, prefix, contract in re.findall(r"^#define\s+(\w+)\(\s*name\s*,\s*n\s*,\s*args_t\s*\)\s+(\w+)_##name\s*=\s*SYS_DEVICE_OP\(\s*(\w+)\s*,\s*n\s*\)", text, re.MULTILINE):
            table = re.search(r"enum\s*\{[^}]*?(\w+)\(\s*" + re.escape(id_macro) + r"\s*\)", text)
            if table:
                found.append((table.group(1), prefix, contract, path, "ops"))
        for id_macro, prefix, contract in re.findall(r"^#define\s+(\w+)\(\s*name\s*,\s*n\s*,\s*flags\s*\)\s+(\w+)_##name\s*=\s*SYS_DEVICE_OP\(\s*(\w+)\s*,\s*n\s*\)", text, re.MULTILINE):
            table = re.search(r"enum\s*\{\s*(\w+)\(\s*" + re.escape(id_macro) + r"\s*\)", text)
            if table:
                found.append((table.group(1), prefix, contract, path, "events"))
    return found


def build_device_contracts():
    enums = enum_catalog()
    contract_enum = {m["name"]: m for m in enums["device_contract"]["members"]}
    contracts = {}
    for symbol, member in contract_enum.items():
        contracts[symbol] = {"name": symbol[len("SYS_DEVICE_CONTRACT_"):], "symbol": symbol, "id": member["value"], "alias": member["alias"],
                             "description": member.get("description", ""), "ops": [], "events": []}
    for table, prefix, contract, path, kind in contract_tables():
        if contract not in contracts:
            fail(f"{rel(path)}: {table} uses {contract}, which is not in the published device_contract enum")
        text = read(path)
        body = macro_body(text, table)
        if body is None:
            fail(f"{rel(path)}: the table macro {table}(X) was not found")
        entry = contracts[contract]
        near = [path]
        seen = []
        for name, n, third, comment in [(m.group(1), m.group(2), m.group(3), m.group("comment")) for m in ROW_RE.finditer(body)]:
            where = f"{rel(path)}: {table} {name}"
            number = safe_int(n, where)
            if number in seen:
                fail(f"{where}: number {number:#04x} is used twice in {table}")
            seen.append(number)
            symbol = f"{prefix}_{name}"
            value = (entry["id"] << 8) | number
            if kind == "ops":
                allowed = OP_TAGS - {"args", "tail", "tail-alias", "tail-description", "urgent"}
                lead, tags = parse_tags(comment or "", allowed, where)
                if lead:
                    fail(f"{where}: text `{lead}` before the first tag (write `@alias <name> @description <text>`)")
                if not tags.get("alias") or not tags.get("description"):
                    fail(f"{where}: its row comment needs `/* @alias <name> @description <text> */`")
                op = {"name": name, "symbol": symbol, "id": value, "op": number, "alias": tags["alias"], "description": tags["description"]}
                if third != "sys_device_none_t":
                    args = struct_fields(third, near, where)
                    struct_check(text, third, args["size"], where)
                    op["args"] = args
                if "returns" in tags:
                    op["returns"] = type_spec(tags["returns"], tags, "returns", near, where)
                if "returns-tail" in tags:
                    op["returns_tail"] = type_spec(tags["returns-tail"], tags, "returns-tail", near, where)
                entry["ops"].append(op)
            else:
                lead, tags = parse_tags(comment or "", EVENT_TAGS, where)
                if lead or not tags.get("alias") or not tags.get("description"):
                    fail(f"{where}: its row comment needs `/* @alias <name> @description <text> [@value <what the value is>] [@unit <u>] */`")
                flags = third
                event = {"name": name, "symbol": symbol, "id": value, "event": number, "alias": tags["alias"], "description": tags["description"],
                         "notify": "NOTIFY" in flags}
                if tags.get("value"):
                    event["value"] = {"description": tags["value"], **({"unit": tags["unit"]} if tags.get("unit") else {})}
                entry["events"].append(event)
    # CREATE has no table row: it is `[type u16][the device type's create struct]`
    device = contracts.get("SYS_DEVICE_CONTRACT_DEVICE")
    if device is not None:
        device["ops"].insert(0, {"name": "CREATE", "symbol": "SYS_DEVICE_OP_CREATE", "id": device["id"] << 8, "op": 0, "alias": "Create device",
                                 "description": "Creates a device of a type: its type id, then the create struct of that type (the device descriptor lists its fields).",
                                 "args": {"struct": None, "size": 2, "fields": [{"name": "type", "type": "u16", "size": 2, "offset": 0, "alias": "Device type", "id_kind": "device-type"}]},
                                 "tail": {"type": "struct", "alias": "Create struct", "description": "The create struct of the chosen type"}})
    out = [c for c in sorted(contracts.values(), key=lambda c: c["id"]) if c["ops"] or c["events"]]
    for c in out:
        if not c["events"]:
            del c["events"]
    return out


# ---------------------------------------------------------------------------
# Frame
# ---------------------------------------------------------------------------

def frame():
    enums = enum_catalog()
    stream = sdkconfig().get("CONFIG_ROUTER_RESPONSE_STREAM")
    if stream is None:
        fail("CONFIG_ROUTER_RESPONSE_STREAM is not set in sdkconfig")
    return {
        "request": {"bytes": ["seq", "flags", "key", "op", "payload"], "flags": {"urgent": {"bit": 0, "description": "The app decides what is urgent; the firmware has no list."}},
                    "device_request": ["seq", "flags", "key = device", "device id (u16)", "op (u16)", "payload"]},
        "response": {"stream": stream, "bytes": ["stream", "seq", "status", "data"], "matching": "seq",
                     "status_enum": "router_status_e", "data": "the op's `returns` (and `returns_tail`) when the status is OK, the error record otherwise"},
        "byte_order": "little-endian",
        "keys_enum": "router_key",
        "status": {m["name"]: m["value"] for m in enums["router_status_e"]["members"]},
    }


def render():
    document = {"schemaVersion": SCHEMA_VERSION, "kind": "contracts", "frame": frame(), "id_kinds": ID_KINDS,
                "routers": build_routers(), "device_contracts": build_device_contracts()}
    return json.dumps(document, indent=2, ensure_ascii=False) + "\n"


def main(argv):
    content = render()
    if "--check" in argv:
        if not OUT.exists() or OUT.read_text(encoding="utf-8").replace("\r\n", "\n") != content:
            print(f"out of date: {rel(OUT)}")
            return 1
        print("contracts: up to date")
        return 0
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(content)
    doc = json.loads(content)
    ops = sum(len(r.get("ops", [])) for r in doc["routers"])
    print(f"Wrote {rel(OUT)}: {len(doc['routers'])} router keys ({ops} ops), {len(doc['device_contracts'])} device contracts")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
