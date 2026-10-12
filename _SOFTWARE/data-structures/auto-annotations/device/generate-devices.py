"""The device descriptors: what the app needs to add, configure and use each device type, one JSON per device.

Grammar and rules: device-annotations.md. The device's own header and .c are the only source:

  * the **device record** in `device_<chip>.h`: `//#device` with `//@<tag> <text>` lines under it (title, description, protocol, tags,
    datasheet, which pins a contract's ops take, which events it publishes, which values depend on its settings);
  * the **create struct** (`.cfg_size = sizeof(<cfg_t>)` in `SYS_DEVICE_TYPE_DEFINE`): its fields carry `@alias`, `@unit`, `@min`, `@max`,
    `@default`, `@one-of`, `@enum-ref`, ... and a pin on another device is a `sys_io_pin_t` field with `@modes`;
  * the **op table** (`SYS_DEVICE_OPS(k_ops)`): `IO_ENTRY(SET_LEVEL)` rows say which contract ops the device implements (their
    names, ids and arguments come from the contract catalog, contracts/generate-contracts.py); `{OWN_OP, handler}` rows are the
    device's own ops, annotated like a router op (`//@op ...` above the row).

Every name a user sees is an explicit `@alias`. Output: data-structures/devices/<device id>.generated.json (LF endings;
generated, do not edit) and `<device id>.md` (the user guide, copied when `device_<chip>.md` exists next to the header).

Usage:
  python data-structures/auto-annotations/device/generate-devices.py [--check]
"""
import importlib.util
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
COMPONENTS_DIR = ROOT / "components"
DEVICES_DIR = COMPONENTS_DIR / "devices"
SDKCONFIG = ROOT / "sdkconfig"
OUT_DIR = ROOT / "data-structures" / "devices"
CONTRACTS_PATH = ROOT / "data-structures" / "auto-annotations" / "contracts" / "generate-contracts.py"
ENUMS_PATH = ROOT / "data-structures" / "auto-annotations" / "enums" / "generate-enums.py"
SCHEMA_VERSION = 2

RECORD_START_RE = re.compile(r"^//#device\s*$")
DIRECTIVE_RE = re.compile(r"^//@(?P<keyword>[a-z][a-z0-9-]*)(?:[ \t]+(?P<rest>.*))?$")
TAG_RE = re.compile(r"(?:^|(?<=\s))@([a-z][a-z0-9-]*)(?=\s|$)")
TYPE_DEFINE_RE = re.compile(r"SYS_DEVICE_TYPE_DEFINE\((?P<body>.*?)\)\s*$", re.DOTALL | re.MULTILINE)
RECORD_TAGS = {"alias": "one", "description": "one", "protocol": "one", "tags": "one", "datasheet": "one", "pins": "many", "param": "many",
               "event": "many", "value": "many", "pwm-frequencies": "one"}
REQUIRED_RECORD_TAGS = ("alias", "description", "protocol")
PROTOCOLS = {"i2c", "native", "pwm"}
CFG_TAGS = {"alias", "description", "note", "unit", "min", "max", "default", "one-of", "enum-ref", "choices", "modes", "default-mode", "required", "aliases"}
CFG_FLAGS = {"required"}
PINS_TAGS = {"one-of", "alias", "note"}
PARAM_TAGS = {"alias", "note", "min", "max", "default", "one-of", "device-wide"}
EVENT_TAGS = {"channel"}
VALUE_TAGS = {"when", "title", "unit", "range"}
FLAG_TAGS = {"required", "device-wide"}
OWN_OP_TAGS = {"alias", "description", "returns", "returns-alias", "returns-description", "unit", "choices", "returns-tail", "returns-tail-alias",
               "returns-tail-description"}


def fail(message):
    raise SystemExit(f"ERROR: {message}")


def rel(path):
    return path.relative_to(ROOT).as_posix()


_CACHE = {}


def load_module(key, path):
    if key not in _CACHE:
        spec = importlib.util.spec_from_file_location(key, path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        module.ROOT, module.COMPONENTS_DIR, module.SDKCONFIG = ROOT, COMPONENTS_DIR, SDKCONFIG
        if hasattr(module, "ENUMS_PATH"):
            module.ENUMS_PATH = ENUMS_PATH
        _CACHE[key] = module
    return _CACHE[key]


def contracts():
    return load_module("gen_contracts", CONTRACTS_PATH)


def enums():
    return load_module("gen_enums", ENUMS_PATH)


def parse_tags(text, allowed, context, flags=FLAG_TAGS):
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
        if name in flags and value:
            fail(f"{context}: @{name} takes no value")
        tags[name] = value
    return lead, tags


# ---------------------------------------------------------------------------
# Values: numbers, $SYMBOLs, lists with ranges
# ---------------------------------------------------------------------------

def symbols():
    if "symbols" not in _CACHE:
        _CACHE["symbols"] = enums().scan()
    return _CACHE["symbols"]


def symbol_member(symbol, context):
    enum_name = symbols()[1].get(symbol)
    if enum_name is None:
        fail(f"{context}: ${symbol} is not a member of a published //#ref-enum")
    member = next(m for m in symbols()[0][enum_name]["members"] if m["name"] == symbol)
    return enum_name, member


def value_of(text, context):
    """A number, a C constant (`#define` / CONFIG_*) or a `$SYMBOL` (a member of a published enum) as an integer."""
    text = text.strip()
    if text.startswith("$"):
        return symbol_member(text[1:], context)[1]["value"]
    return contracts().eval_c(text, context)


def parse_list(text, context):
    """`[a, b, 0x10..0x17, $SYMBOL]` as the json carries it: numbers, or `{symbol, value, alias}` for a `$SYMBOL`."""
    text = text.strip()
    if not (text.startswith("[") and text.endswith("]")):
        fail(f"{context}: a list is written [a, b, a..b], got `{text}`")
    out = []
    for item in [i.strip() for i in text[1:-1].split(",") if i.strip()]:
        if ".." in item and not item.startswith("$"):
            low, high = (value_of(part, context) for part in item.split("..", 1))
            if high < low:
                fail(f"{context}: range {item} is backwards")
            out.extend(range(low, high + 1))
        elif item.startswith("$"):
            enum_name, member = symbol_member(item[1:], context)
            out.append({"symbol": member["name"], "enum": enum_name, "value": member["value"], "alias": member["alias"]})
        else:
            out.append(value_of(item, context))
    if not out:
        fail(f"{context}: an empty list")
    return out


# ---------------------------------------------------------------------------
# The record
# ---------------------------------------------------------------------------

def scan_record(label, text):
    """{tags: {keyword: value | [values]}} from the `//#device` line and the `//@` lines under it; None without one."""
    lines = text.split("\n")
    heads = [i for i, line in enumerate(lines) if RECORD_START_RE.match(line)]
    if not heads:
        return None
    if len(heads) > 1:
        fail(f"{label}: {len(heads)} `//#device` records: one device per header")
    entries, i = [], heads[0] + 1
    while i < len(lines) and lines[i].startswith("//"):
        d = DIRECTIVE_RE.match(lines[i])
        if d:
            keyword = d.group("keyword")
            if keyword not in RECORD_TAGS:
                fail(f"{label}:{i + 1}: unknown device tag //@{keyword} (known: {', '.join(sorted(RECORD_TAGS))})")
            entries.append([keyword, (d.group("rest") or "").strip(), i + 1])
        else:
            if not entries:
                fail(f"{label}:{i + 1}: a comment line right under `//#device` before any //@ tag")
            entries[-1][1] = (entries[-1][1] + " " + lines[i][2:].strip()).strip()
        i += 1
    tags = {}
    for keyword, value, line in entries:
        if RECORD_TAGS[keyword] == "many":
            tags.setdefault(keyword, []).append({"text": value, "line": line})
        elif keyword in tags:
            fail(f"{label}:{line}: //@{keyword} given twice")
        else:
            tags[keyword] = value
    return tags


# ---------------------------------------------------------------------------
# The create struct
# ---------------------------------------------------------------------------

def cfg_struct_name(c_text, context):
    m = TYPE_DEFINE_RE.search(c_text)
    if not m:
        fail(f"{context}: no SYS_DEVICE_TYPE_DEFINE(...) in the .c file")
    body = m.group("body")
    type_m = re.search(r"\.type\s*=\s*(SYS_DEVICE_TYPE_\w+)", body)
    cfg_m = re.search(r"\.cfg_size\s*=\s*sizeof\(\s*(\w+)\s*\)", body)
    if not type_m or not cfg_m:
        fail(f"{context}: SYS_DEVICE_TYPE_DEFINE needs .type = SYS_DEVICE_TYPE_<NAME> and .cfg_size = sizeof(<cfg struct>)")
    return type_m.group(1), cfg_m.group(1)


def type_ids():
    if "types" not in _CACHE:
        text = (COMPONENTS_DIR / "sys" / "sys_device" / "sys_device_types.h").read_text(encoding="utf-8")
        _CACHE["types"] = {f"SYS_DEVICE_TYPE_{name}": contracts().safe_int(value, name) for name, value in re.findall(r"X\(\s*(\w+)\s*,\s*(0x[0-9a-fA-F]+|\d+)\s*\)", text)}
    return _CACHE["types"]


# ---------------------------------------------------------------------------
# One device
# ---------------------------------------------------------------------------

def entry_macros():
    """{`IO_ENTRY`: op symbol prefix `IO_OP`} from `#define X_ENTRY(name) {PFX_##name, handler}`."""
    if "entries" not in _CACHE:
        found = {}
        for path in sorted(COMPONENTS_DIR.rglob("*.h")):
            for macro, prefix in re.findall(r"^#define\s+(\w+_ENTRY)\(\s*name\s*\)\s*\{\s*(\w+)_##name\s*,", path.read_text(encoding="utf-8", errors="ignore"), re.MULTILINE):
                found[macro] = prefix
        _CACHE["entries"] = found
    return _CACHE["entries"]


def op_table(c_text, context):
    m = re.search(r"static\s+const\s+sys_device_op_t\s+k_ops\[\]\s*=\s*\{", c_text)
    if not m:
        fail(f"{context}: no `static const sys_device_op_t k_ops[] = {{...}};`")
    depth, end = 1, m.end()
    while depth and end < len(c_text):
        depth += (c_text[end] == "{") - (c_text[end] == "}")
        end += 1
    rows, pending = [], []
    for line in c_text[m.end():end - 1].split("\n"):
        stripped = line.strip()
        if stripped.startswith("//@op") or stripped.startswith("//@internal"):
            pending.append(stripped)
            continue
        code = re.sub(r"//.*$", "", line)
        code = re.sub(r"\b(\w+_ENTRIES)\b", lambda m: contracts().defines().get(m.group(1), m.group(0)), code)  # a macro that stands for several rows
        for macro, name in re.findall(r"\b(\w+_ENTRY)\(\s*(\w+)\s*\)", code):
            rows.append(("entry", macro, name, []))
        for symbol, handler in re.findall(r"\{\s*(\w+_OP_\w+|\w+)\s*,\s*(\w+)\s*\}", code):
            if not symbol.endswith("_ENTRY"):
                if not any(p.startswith("//@internal") for p in pending):  # `//@internal` above a row: a firmware-internal op, not the app's
                    rows.append(("own", symbol, handler, pending))
                pending = []
    return rows


def build_device(path, text, tags):
    cm = contracts()
    label = rel(path)
    folder = path.parent.name
    for key in REQUIRED_RECORD_TAGS:
        if not tags.get(key):
            fail(f"{label}: the device record needs `//@{key} ...`")
    protocols = tags["protocol"].split()
    if not protocols or set(protocols) - PROTOCOLS:
        fail(f"{label}: //@protocol is a list of {sorted(PROTOCOLS)}, got `{tags['protocol']}`")
    c_path = path.with_suffix(".c")
    if not c_path.exists():
        fail(f"{label}: {rel(c_path)} not found")
    c_text = cm.read(c_path)
    type_symbol, cfg_name = cfg_struct_name(c_text, rel(c_path))
    if type_symbol not in type_ids():
        fail(f"{rel(c_path)}: {type_symbol} is not in sys_device_types.h")
    device = {"schemaVersion": SCHEMA_VERSION, "kind": "device-definition", "id": folder, "alias": tags["alias"], "description": tags["description"],
              "protocols": protocols, "source_file": label, "type": {"symbol": type_symbol, "id": type_ids()[type_symbol]}}
    if tags.get("tags"):
        device["tags"] = tags["tags"].split()
    if tags.get("datasheet"):
        device["datasheet"] = tags["datasheet"]

    # the create struct
    cfg = build_create(cfg_name, [path, c_path], label)
    device["create"] = {"frame": ["key 0x05", "device id (u16)", "op 0x0000 (CREATE)", "type (u16)", "the fields below, in order"], **cfg}

    # the contracts the op table implements
    catalog = {c["symbol"]: c for c in cm.build_device_contracts()}
    by_op = {op["symbol"]: (c, op) for c in catalog.values() for op in c["ops"]}
    rows = op_table(c_text, rel(c_path))
    implemented = {}
    own_ops = []
    for kind, a, b, pending in rows:
        if kind == "entry":
            prefix = entry_macros().get(a)
            if prefix is None:
                fail(f"{rel(c_path)}: {a}({b}) is not an entry macro of a contract")
            symbol = f"{prefix}_{b}"
            if symbol == "EVENT_OP_ON_EVENT":
                continue  # the listener hook sys_event calls: firmware-internal
            if symbol not in by_op:
                fail(f"{rel(c_path)}: {a}({b}): {symbol} is not an op of a device contract")
            contract, op = by_op[symbol]
            implemented.setdefault(contract["symbol"], []).append(op)
        else:
            own_ops.append(build_own_op(a, b, pending, c_text, [path, c_path], rel(c_path)))
    pins = parse_pins_tags(tags.get("pins", []), label)
    params = parse_params(tags.get("param", []), by_op, label)
    device["contracts"] = []
    for contract_symbol, ops in implemented.items():
        contract = catalog[contract_symbol]
        entry = {"contract": contract["name"], "id": contract["id"], "alias": contract["alias"], "ops": []}
        for op in ops:
            item = {"name": op["name"], "symbol": op["symbol"], "id": op["id"], "alias": op["alias"], "description": op["description"]}
            if "returns" in op:
                item["returns"] = op["returns"]
            fields = [f["name"] for f in op.get("args", {}).get("fields", [])]
            narrowed = []
            for f in op.get("args", {}).get("fields", []):
                p = {}
                if f.get("id_kind") in ("pin", "channel") and (contract["name"] in pins or "*" in pins):
                    p.update(pins.get(contract["name"]) or pins["*"])
                p.update(params.get((op["symbol"], f["name"]), {}))
                if p:
                    narrowed.append({"field": f["name"], **p})
            for (sym, field) in params:
                if sym == op["symbol"] and field not in fields:
                    fail(f"{label}: //@param {sym}.{field}: that op has no field `{field}`")
            if narrowed:
                item["params"] = narrowed
            entry["ops"].append(item)
        device["contracts"].append(entry)
    for (sym, field) in params:
        if sym not in by_op or not any(sym == op["symbol"] for ops in implemented.values() for op in ops):
            fail(f"{label}: //@param {sym}.{field}: the device does not implement {sym}")
    if own_ops:
        device["own_ops"] = own_ops
    # events
    events = []
    for entry in tags.get("event", []):
        where = f"{label}: //@event (line {entry['line']})"
        lead, etags = parse_tags(entry["text"], EVENT_TAGS, where)
        names = lead.split()
        if not names:
            fail(f"{where}: `//@event <EVENT_SYMBOL> ... [@channel <text>]`")
        listed = []
        for name in names:
            found = [(c, e) for c in catalog.values() for e in c.get("events", []) if e["symbol"] == name]
            if not found:
                fail(f"{where}: {name} is not an event of a device contract")
            contract, event = found[0]
            if contract["symbol"] not in implemented and contract["name"] not in ("POWER_MANAGER",):
                fail(f"{where}: {name} belongs to the {contract['name']} contract, which the device does not implement")
            listed.append({"symbol": name, "id": event["id"], "alias": event["alias"], "description": event["description"], "contract": contract["name"]})
        item = {"events": listed}
        if etags.get("channel"):
            item["channel"] = {"alias": etags["channel"]}
            if pins.get(listed[0]["contract"]) or pins.get("*"):
                item["channel"].update(pins.get(listed[0]["contract"]) or pins["*"])
        events.append(item)
    if events:
        device["events"] = events
    values = parse_values(tags.get("value", []), device["create"]["fields"], label)
    if values:
        device["values"] = values
    if tags.get("pwm-frequencies"):
        words = tags["pwm-frequencies"].split()
        count = value_of(words[0], label)
        device["pwm_frequencies"] = bin(count).count("1") if "@count-bits" in words[1:] else count
    return device


def expand_per_element(text, count, context, what):
    """`a|b|c` is one entry per element; `x*4` repeats an entry; a single entry applies to every element."""
    out = []
    for part in [p.strip() for p in text.split("|")]:
        m = re.fullmatch(r"(.+?)\s*\*\s*(\d+)", part)
        out.extend([m.group(1).strip()] * int(m.group(2)) if m else [part])
    if len(out) == 1:
        out = out * count
    if len(out) != count:
        fail(f"{context}: {what} names {len(out)} element(s), the array has {count}")
    return out


def finalize_field(field, where):
    """Resolve the tags a device field kept (`one-of`, `modes`, `default`, ...) into values; drops `_tags`."""
    tags = field.pop("_tags", {})
    ctx = f"{where}.{field['name']}"
    if tags.get("note"):
        field["note"] = tags["note"]
    if "required" in tags:
        field["required"] = True
    if "one-of" in tags:
        field["one_of"] = parse_list(tags["one-of"], ctx)
    if "default" in tags and "default" in field:
        text = tags["default"].strip()
        if text.startswith("$"):
            enum_name, member = symbol_member(text[1:], ctx)
            field["default"] = {"symbol": member["name"], "enum": enum_name, "value": member["value"], "alias": member["alias"]}
        else:
            field["default"] = value_of(text, ctx)
    if field.get("struct") == "sys_io_pin_t":
        field["pin_ref"] = True
    count = field.get("array_len")
    if count and "aliases" in tags:
        field["aliases"] = expand_per_element(tags["aliases"], count, ctx, "@aliases")
    elif count and field.get("pin_ref"):
        fail(f"{ctx}: an array of pins needs `@aliases a|b|c` (one name per element)")
    if "modes" in tags:
        if count:
            field["modes"] = [parse_list(x, ctx) for x in expand_per_element(tags["modes"], count, ctx, "@modes")]
        else:
            field["modes"] = parse_list(tags["modes"], ctx)
    if "default-mode" in tags:
        if count:
            field["default_mode"] = [value_of(x, ctx) if x else None for x in expand_per_element(tags["default-mode"], count, ctx, "@default-mode")]
        else:
            field["default_mode"] = value_of(tags["default-mode"], ctx)
    if "modes" in field and "default_mode" in field and not count:
        values = [m["value"] if isinstance(m, dict) else m for m in field["modes"]]
        if field["default_mode"] not in values:
            fail(f"{ctx}: @default-mode is not one of @modes")
    for key in ("one_of",):
        if key in field and "default" in field and not isinstance(field["default"], dict):
            values = [m["value"] if isinstance(m, dict) else m for m in field[key]]
            if field["default"] not in values:
                fail(f"{ctx}: @default {field['default']} is not one of @one-of")
    if field.get("fields"):
        for inner in field["fields"]:
            inner.pop("_tags", None)
    return field


def build_create(struct, near, label):
    cm = contracts()
    cm.FIELD_TAGS = set(cm.FIELD_TAGS_BASE) | CFG_TAGS | {"aliases"}
    cm.FLAG_TAGS = set(cm.FLAG_TAGS) | CFG_FLAGS
    cm.KEEP_TAGS = True
    if struct == "sys_device_none_t" or any(re.search(r"typedef\s+sys_device_none_t\s+" + re.escape(struct) + r"\s*;", cm.read(p)) for p in near):
        cm.KEEP_TAGS = False
        return {"struct": struct, "size": 0, "fields": []}  # the device takes no create struct
    try:
        body, _ = cm.find_struct(struct, near)
        if not body.strip():
            return {"struct": struct, "size": 0, "fields": []}
        layout = cm.struct_fields(struct, near, f"{label}: {struct}")
    finally:
        cm.KEEP_TAGS = False
    fields = [finalize_field(f, f"{label}: {struct}") for f in layout["fields"]]
    return {"struct": struct, "size": layout["size"], "source_file": layout["source_file"], "fields": fields}


def parse_pins_tags(entries, label):
    out = {}
    for entry in entries:
        where = f"{label}: //@pins (line {entry['line']})"
        lead, tags = parse_tags(entry["text"], PINS_TAGS, where)
        if not lead:
            fail(f"{where}: `//@pins <CONTRACT|*> @one-of [..] @alias <name>`")
        if "one-of" not in tags or not tags.get("alias"):
            fail(f"{where}: needs @one-of and @alias")
        item = {"one_of": parse_list(tags["one-of"], where), "alias": tags["alias"]}
        if tags.get("note"):
            item["note"] = tags["note"]
        out[lead] = item
    return out


def parse_params(entries, by_op, label):
    out = {}
    for entry in entries:
        where = f"{label}: //@param (line {entry['line']})"
        lead, tags = parse_tags(entry["text"], PARAM_TAGS, where)
        m = re.fullmatch(r"(\w+)\.(\w+)", lead)
        if not m:
            fail(f"{where}: `//@param <OP_SYMBOL>.<field> [tags]`")
        item = {}
        for key in ("alias", "note"):
            if tags.get(key):
                item[key] = tags[key]
        for key in ("min", "max", "default"):
            if key in tags:
                item[key] = value_of(tags[key], where)
        if "one-of" in tags:
            item["one_of"] = parse_list(tags["one-of"], where)
        if "device-wide" in tags:
            item["device_wide"] = True
        out[(m.group(1), m.group(2))] = item
    return out


def parse_values(entries, fields, label):
    """`//@value NAME @when f=v ... @title T [@unit u] [@range a..b]`: what one number means by the install settings; the first variant whose @when holds applies."""
    names = {f["name"] for f in fields}
    values = {}
    for entry in entries:
        where = f"{label}: //@value (line {entry['line']})"
        lead, tags = parse_tags(entry["text"], VALUE_TAGS, where)
        if not re.fullmatch(r"[A-Z][A-Z0-9_]*", lead) or not tags.get("title"):
            fail(f"{where}: `//@value NAME @when f=v ... @title <name> [@unit u] [@range a..b]`")
        variant = {"title": tags["title"]}
        conditions = []
        for pair in tags.get("when", "").split():
            m = re.fullmatch(r"(\w+)=(.+)", pair)
            if not m or m.group(1) not in names:
                fail(f"{where}: @when `{pair}` is `<install field>=<value>`")
            conditions.append({"field": m.group(1), "value": value_of(m.group(2), where)})
        if conditions:
            variant["when"] = conditions
        if tags.get("unit"):
            variant["unit"] = tags["unit"]
        if tags.get("range"):
            m = re.fullmatch(r"(\S+)\.\.(\S+)", tags["range"])
            if not m:
                fail(f"{where}: @range is `min..max`")

            def bound(token):
                return {"field": token} if token in names else value_of(token, where)
            variant["range"] = {"min": bound(m.group(1)), "max": bound(m.group(2))}
        values.setdefault(lead, []).append(variant)
    return values


def build_own_op(symbol, handler, pending, c_text, near, context):
    cm = contracts()
    where = f"{context}: own op {symbol}"
    head = [p for p in pending if re.match(r"^//@op\s", p)]
    if len(head) != 1:
        fail(f"{where}: needs one `//@op {symbol} @alias <name> @description <text>` line above its row")
    lead, tags = parse_tags(head[0][len("//@op"):], OWN_OP_TAGS, where, set())
    if lead != symbol or not tags.get("alias") or not tags.get("description"):
        fail(f"{where}: the `//@op` line must name {symbol} and give @alias and @description")
    ids = cm.defines()
    if symbol not in ids:
        fail(f"{where}: no `#define {symbol} SYS_DEVICE_OWN_OP(n)`")
    m = re.search(r"SYS_DEVICE_OWN_OP\(\s*(0x[0-9a-fA-F]+|\d+)\s*\)", ids[symbol])
    if not m:
        fail(f"{where}: {symbol} is not a SYS_DEVICE_OWN_OP(n)")
    op = {"symbol": symbol, "id": 0xFF00 | cm.safe_int(m.group(1), where), "alias": tags["alias"], "description": tags["description"]}
    hm = re.search(r"SYS_DEVICE_HANDLER\(\s*" + re.escape(handler) + r"\s*,\s*(\w+)\s*\)", c_text)
    if hm and hm.group(1) != "sys_device_none_t":
        args = cm.struct_fields(hm.group(1), near, where)
        op["args"] = args
    if "returns" in tags:
        op["returns"] = cm.type_spec(tags["returns"], tags, "returns", near, where)
    if "returns-tail" in tags:
        op["returns_tail"] = cm.type_spec(tags["returns-tail"], tags, "returns-tail", near, where)
    return op


# ---------------------------------------------------------------------------
# Whole run
# ---------------------------------------------------------------------------

def render(document):
    return json.dumps(document, indent=2, ensure_ascii=False) + "\n"


def outputs():
    files = {}
    seen = set()
    for path in sorted(DEVICES_DIR.glob("device_*/device_*.h")):
        text = contracts().read(path)
        tags = scan_record(rel(path), text)
        if tags is None:
            continue
        device = build_device(path, text, tags)
        if device["id"] in seen:
            fail(f"{rel(path)}: duplicate device id {device['id']}")
        seen.add(device["id"])
        guide = path.with_suffix(".md")
        if guide.exists():
            device["guide"] = f"{device['id']}.md"
            files[OUT_DIR / device["guide"]] = guide.read_text(encoding="utf-8").replace("\r\n", "\n")
        files[OUT_DIR / f"{device['id']}.generated.json"] = render(device)
    return files


def main(argv):
    files = outputs()
    if "--check" in argv:
        stale = [rel(p) for p, c in files.items() if not p.exists() or p.read_text(encoding="utf-8").replace("\r\n", "\n") != c]
        if stale:
            print("out of date:", *stale, sep="\n  ")
            return 1
        print("devices: up to date")
        return 0
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for old in OUT_DIR.glob("device_*.generated.json"):
        if old not in files:
            old.unlink()
    for path, content in files.items():
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(content)
    print(f"Wrote {rel(OUT_DIR)}: {sum(1 for p in files if p.name.endswith('.generated.json'))} devices")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
