"""
Device JSON generator for components/codecs/decoders/**/dec_device_*.h.

Reads the device records (//#device, //#self-property, //#property, //#contract, each
continued by `//  @tag ...` lines) plus the field-level @tag comments used across every
dec_*.h packet struct, and renders one self-contained JSON descriptor per annotated device
header. See data-structures/auto-annotations/device/device-annotations.md for the grammar.

Design notes:
  - Every record kind lists the tags it accepts; an unknown tag fails generation, so a typo
    or a tag nothing reads never lands silently. The old one-line //@id / //@contract
    directives fail with a pointer to the record form.
  - A sys_io_pin_ref_t install field expands to its three wire fields (<name>_device_id,
    <name>_pin, <name>_mode) and one pin group keyed by <name>, the same key the board
    descriptor uses for the pins its devices take.
  - This replaces the older generate-device-json.py, which used a plain
    @capability directive and left enum symbols as bare unchecked strings.
    That's gone - @self-property/@property + @arg + $SYMBOL resolution is the
    only supported form now (see the .md).
  - Enum data comes from data-structures/auto-annotations/enums/generate-enums.py's scan(),
    imported directly and re-run live on every invocation - a loose coupling
    on purpose: no build-order dependency on a committed enums.json, no risk
    of resolving against a stale file. Resolution of any individual $SYMBOL
    is still strict: unresolved is a hard error, never a silent string
    fallback, same as an @arg pointing at an undefined property.
  - Contract completeness is enforced: every field the referenced generic
    packet marks @required must appear in the contract's own @param list, or
    generation fails - a device can't silently omit a required wire field.
  - Class bytes are resolved by packet_classes.py (scan_all_class_headers /
    scan_kconfig_class_headers / resolve_class), shared with the other generators.
  - Every generated document is validated against device-definition.schema.json
    before it's written - the "$schema" field it carries is a real, checked
    contract, not a dangling label. jsonschema is optional: if it isn't
    installed, generation still proceeds but prints a loud warning instead of
    silently skipping the check, since a missing safety net should be visible.
"""
import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Tuple

try:
    import jsonschema
except ImportError:
    jsonschema = None

PROJECT_ROOT = Path(__file__).parents[3]  # _SOFTWARE folder
SCHEMA_PATH = Path(__file__).parents[2] / "schema" / "device-definition.schema.json"
SCHEMA_REL_PATH = SCHEMA_PATH.relative_to(PROJECT_ROOT).as_posix()  # single source of truth for the "$schema" value - see validate_document()
sys.path.insert(0, str(Path(__file__).parents[1]))
sys.path.insert(0, str(Path(__file__).parents[1] / "enums"))
from packet_classes import scan_all_class_headers, scan_kconfig_class_headers, resolve_class  # reuse, don't duplicate

import importlib.util as _ilu  # generate-enums.py has a hyphenated filename, so import it by path instead of by module name
_spec = _ilu.spec_from_file_location("generate_enums", Path(__file__).parents[1] / "enums" / "generate-enums.py")
generate_enums = _ilu.module_from_spec(_spec)
_spec.loader.exec_module(generate_enums)

SDKCONFIG_PATH = PROJECT_ROOT / "sdkconfig"

PACKET_HEADER_RE = re.compile(r"#define\s+HEADER_(packet_\w+)\s+(0x[0-9A-Fa-f]+)")
STRUCT_RE = re.compile(r"typedef\s+struct\s+(?:__packed\s*)?\{(.*?)\}\s*(packet_\w+_t)\s*;", re.DOTALL)
# A device that takes its create frame as it is: the cfg struct in its own header IS the packet.
DEVICE_CFG_RE = re.compile(r"typedef\s+struct\s+(?:__packed\s*)?\{(.*?)\}\s*(d_\w+_cfg_t)\s*;", re.DOTALL)
CREATE_FRAME_CLASS = "SYS_CONTRACTS"  # create frames ([0x00][type_id][cfg]) travel in class 0x01
FIELD_LINE_RE = re.compile(
    r"^\s*(?P<type>[A-Za-z_][\w ]*?)\s+(?P<name>\w+)\s*(?:\[\s*(?P<arr>\w*)\s*\])?\s*;\s*(?://\s*(?P<comment>.*))?\s*$"
)
TAG_RE = re.compile(r"@([\w-]+)\b")
DEFINE_RE = re.compile(r"#define\s+([A-Z][A-Z0-9_]*)\s+([^\s/][^\n/]*)")
SDKCONFIG_RE = re.compile(r"^(CONFIG_\w+)=(.+)$", re.MULTILINE)
# `//#<kind> <name> [@tag ...]` starts a record; `//  <text>` (two or more spaces) continues it.
RECORD_RE = re.compile(r"^\s*//#(?P<kind>[\w-]+)(?:\s+(?P<rest>.*?))?\s*$")
CONTINUATION_RE = re.compile(r"^\s*//\s{2,}(?P<text>\S.*?)\s*$")
LEGACY_DIRECTIVE_RE = re.compile(r"^\s*//@(id|version|title|description|protocol|tags|datasheet|contract-provider|pwm-frequencies|self-property|property|contract|param|returns)\b")

# Tags each record accepts (normalized: `-` -> `_`).
RECORD_TAGS = {
    "device": {"title", "description", "protocol", "tags", "datasheet", "contract_provider", "pwm_frequencies", "count_bits", "type_id"},
    "self-property": {"one_of", "alias", "note"},
    "property": {"enum_ref", "one_of", "default", "alias", "note"},
    "contract": {"alias", "description", "returns"},
}
PARAM_TAGS = {"arg", "alias", "type", "unit", "one_of", "min", "max", "default", "device_wide", "note"}

# One pin on another device in an install packet (dec_device_common.h): three uint8_t on the wire.
PIN_REF_TYPE = "sys_io_pin_ref_t"
PIN_REF_TAGS = {"alias", "note", "modes", "default_mode"}
MAX_RANGE = 4096


def normalize_tag(name: str) -> str:
    return name.lower().replace("-", "_")


def parse_tags(comment: str) -> Dict[str, str]:
    tags: Dict[str, str] = {}
    matches = list(TAG_RE.finditer(comment))
    if not matches:
        text = comment.strip()
        if text:
            tags["desc"] = text
        return tags
    prefix = comment[: matches[0].start()].strip()
    if prefix:
        tags["desc"] = prefix
    for i, m in enumerate(matches):
        name = normalize_tag(m.group(1))
        start = m.end()
        end = matches[i + 1].start() if i + 1 < len(matches) else len(comment)
        tags[name] = comment[start:end].strip()
    return tags


@dataclass
class Field:
    type: str
    name: str
    array_len: Optional[int]
    flexible_array: bool
    tags: Dict[str, str]
    group_info: Optional[Dict[str, str]] = None  # alias / note of the pin group this field opens


def pin_ref_fields(name: str, arr: Optional[str], tags: Dict[str, str]) -> List[Field]:
    """`sys_io_pin_ref_t <name>; //@alias ... @note ... @modes [...] @default-mode $X` -> its three wire fields."""
    if arr is not None:
        sys.exit(f"ERROR: field '{name}': arrays of {PIN_REF_TYPE} are not supported")
    unknown = sorted(set(tags) - PIN_REF_TAGS)
    if unknown:
        sys.exit(f"ERROR: field '{name}' ({PIN_REF_TYPE}): unsupported tag(s) {unknown}; allowed: {sorted(PIN_REF_TAGS)}")
    mode_tags = {"group": name, "enum_ref": "sys_io_mode_e"}
    if "modes" in tags:
        mode_tags["one_of"] = tags["modes"]
    if "default_mode" in tags:
        mode_tags["default"] = tags["default_mode"]
    info = {key: tags[key] for key in ("alias", "note") if tags.get(key)}
    return [
        Field("uint8_t", f"{name}_device_id", None, False, {"group": name}, info),
        Field("uint8_t", f"{name}_pin", None, False, {"group": name, "sentinel": "SYS_GPIO_NONE"}),
        Field("uint8_t", f"{name}_mode", None, False, mode_tags),
    ]


def parse_struct_fields(body: str, defines: Optional[Dict[str, str]] = None, sdkconfig: Optional[Dict[str, int]] = None) -> List[Field]:
    """Array lengths may be a literal or a symbol (#define / CONFIG_*), resolved one hop.
    An unresolvable symbolic length is a hard error: silently dropping the field would
    shift every following wire offset. A comment-only line `//  ...` (two or more spaces)
    right after a field continues that field's annotation."""
    raw: List[List] = []  # [type, name, arr, comment]
    continuing = False
    for raw_line in body.splitlines():
        line = raw_line.strip()
        m = FIELD_LINE_RE.match(line)
        if m:
            raw.append([m.group("type").strip(), m.group("name"), m.group("arr"), m.group("comment") or ""])
            continuing = True
            continue
        c = CONTINUATION_RE.match(line)
        if c and continuing:
            raw[-1][3] += " " + c.group("text")
            continue
        continuing = False
    fields: List[Field] = []
    for type_, name, arr, comment in raw:
        tags = parse_tags(comment)
        if type_ == PIN_REF_TYPE:
            fields.extend(pin_ref_fields(name, arr, tags))
            continue
        array_len = None
        if arr:
            array_len, shown = resolve_numeric(arr, defines or {}, sdkconfig or {})
            if array_len is None:
                sys.exit(f"ERROR: field '{name}[{arr}]': array length {shown}")
        fields.append(Field(type_, name, array_len, arr == "", tags))
    return fields


def to_int(token: str) -> Optional[int]:
    token = token.strip().strip('"')
    try:
        return int(token, 0)
    except ValueError:
        return None


def scan_defines(root: Path) -> Dict[str, str]:
    defines: Dict[str, str] = {}
    for h in root.rglob("*.h"):
        text = h.read_text(encoding="utf-8", errors="ignore")
        for name, value in DEFINE_RE.findall(text):
            defines.setdefault(name, value.strip())
    return defines


def scan_sdkconfig(path: Path) -> Dict[str, int]:
    if not path.exists():
        return {}
    out: Dict[str, int] = {}
    for name, raw in SDKCONFIG_RE.findall(path.read_text(encoding="utf-8", errors="ignore")):
        v = to_int(raw.strip())
        if v is not None:
            out[name] = v
    return out


def resolve_numeric(token: str, defines: Dict[str, str], sdkconfig: Dict[str, int]) -> Tuple[Optional[int], str]:
    """@min/@max/@default/@sentinel: literal -> sdkconfig -> #define (one hop). Best-effort, unresolved is not fatal here."""
    token = token.strip()
    direct = to_int(token)
    if direct is not None:
        return direct, token
    if token in sdkconfig:
        return sdkconfig[token], f"{sdkconfig[token]} ({token})"
    if token in defines:
        inner = to_int(defines[token]) or to_int(defines.get(defines[token], ""))
        return (inner, f"{inner} ({token})") if inner is not None else (None, f"{defines[token]} ({token}, unresolved)")
    return None, f"{token} (unresolved)"


def resolve_symbol(token: str, symbols: Dict[str, dict], context: str) -> dict:
    """$SYMBOL -> {symbol, enum, value, alias, description}. Hard error if unresolved - this
    is the whole point of the $ marker, see device-annotations.md."""
    name = token[1:].strip()
    if name not in symbols:
        sys.exit(f"ERROR: {context}: unresolved global symbol '{token}' - no //#ref-enum enum defines '{name}'")
    entry = symbols[name]
    return {"symbol": name, "enum": entry["enum"], "value": entry["value"], "alias": entry["alias"], "description": entry["description"]}


def parse_choice_list(raw: str, symbols: Dict[str, dict], context: str) -> List[object]:
    inner = raw.strip().lstrip("[").rstrip("]")
    values: List[object] = []
    for part in inner.split(","):
        token = part.strip()
        if not token:
            continue
        if token.startswith("$"):
            values.append(resolve_symbol(token, symbols, context))
        elif ".." in token:
            # `a..b`: every integer from a to b, both included (`0..7`, `0x10..0x17`).
            low, _, high = token.partition("..")
            first, last = to_int(low), to_int(high)
            if first is None or last is None or last < first or last - first >= MAX_RANGE:
                sys.exit(f"ERROR: {context}: bad range '{token}' (want a..b with integers a <= b, at most {MAX_RANGE} values)")
            values.extend(range(first, last + 1))
        else:
            values.append(to_int(token) if to_int(token) is not None else token)
    return values


def field_json(f: Field, defines: Dict[str, str], sdkconfig: Dict[str, int], symbols: Dict[str, dict], context: str) -> dict:
    out: dict = {"type": f.type}
    if f.array_len:
        out["array_len"] = f.array_len
    if f.flexible_array:
        out["flexible_array"] = True
    if "alias" in f.tags:
        out["alias"] = f.tags["alias"]
    if "required" in f.tags:
        out["required"] = True
    elif "optional" in f.tags:
        out["required"] = False
    if "min" in f.tags:
        out["min"] = resolve_numeric(f.tags["min"], defines, sdkconfig)[0]
    if "max" in f.tags:
        out["max"] = resolve_numeric(f.tags["max"], defines, sdkconfig)[0]
    if "one_of" in f.tags:
        out["one_of"] = parse_choice_list(f.tags["one_of"], symbols, context)
    if "default" in f.tags:
        token = f.tags["default"]
        out["default"] = resolve_symbol(token, symbols, context)["value"] if token.startswith("$") else resolve_numeric(token, defines, sdkconfig)[0]
    if "sentinel" in f.tags:
        out["sentinel"] = resolve_numeric(f.tags["sentinel"], defines, sdkconfig)[0]
    if "unit" in f.tags:
        out["unit"] = f.tags["unit"]
    # @enum-ref is the explicit canonical spelling.  Keep @ref as a read-only
    # compatibility alias so old headers regenerate without losing metadata.
    enum_ref = f.tags.get("enum_ref", f.tags.get("ref"))
    if enum_ref:
        out["enum_ref"] = enum_ref
    if "default" in out and "one_of" in out and out["default"] not in [choice["value"] if isinstance(choice, dict) else choice for choice in out["one_of"]]:
        sys.exit(f"ERROR: {context}: field '{f.name}' @default is not one of its choices")
    if "group" in f.tags:
        out["group"] = f.tags["group"]
    if f.tags.get("desc"):
        out["desc"] = f.tags["desc"]
    if "note" in f.tags:
        out["note"] = f.tags["note"]
    if "encoding" in f.tags:
        out["encoding"] = f.tags["encoding"]
    if "terminator" in f.tags:
        out["terminator"] = f.tags["terminator"]
    return out


def build_packet_entry(name: str, header_value: str, fields: List[Field], source_file: str, class_name, class_hex, defines, sdkconfig, symbols) -> dict:
    context = f"{source_file}: {name}"
    for index, field in enumerate(fields):
        if field.flexible_array and index != len(fields) - 1:
            sys.exit(f"ERROR: {context}: flexible array '{field.name}[]' must be the final wire field")
    order = [f.name for f in fields]
    groups: Dict[str, List[str]] = {}
    group_info: Dict[str, Dict[str, str]] = {}
    for f in fields:
        g = f.tags.get("group")
        if g:
            groups.setdefault(g, []).append(f.name)
            group_info.setdefault(g, {}).update(f.group_info or {})
    entry = {
        "source_file": source_file,
        "class_name": class_name,
        "class_header": class_hex,
        "packet_header": header_value,
        "decoder": f"decoder_{name}()",
        "field_order": order,
        "fields": {f.name: field_json(f, defines, sdkconfig, symbols, context) for f in fields},
    }
    if groups:
        entry["groups"] = {
            gname: {"fields": gfields, "sentinel_field": next((f.name for f in fields if f.name in gfields and "sentinel" in f.tags), None), **group_info.get(gname, {})}
            for gname, gfields in groups.items()
        }
    return entry


def parse_packet_file(path: Path, all_classes: Dict[str, str], defines, sdkconfig, symbols) -> Dict[str, dict]:
    text = path.read_text(encoding="utf-8", errors="ignore")
    class_info = resolve_class(text, all_classes)
    class_name, class_hex = class_info if class_info else (None, None)
    structs = {m.group(2): m.group(1) for m in STRUCT_RE.finditer(text)}
    source_file = path.name
    out = {}
    for header_match in PACKET_HEADER_RE.finditer(text):
        name, hexval = header_match.group(1), header_match.group(2)
        struct_name = name if name in structs else (name + "_t" if (name + "_t") in structs else None)
        if not struct_name:
            continue
        fields = parse_struct_fields(structs[struct_name], defines, sdkconfig)
        out[struct_name] = build_packet_entry(struct_name, hexval, fields, source_file, class_name, class_hex, defines, sdkconfig, symbols)
    return out


def device_header_record(path: Path) -> Optional[Tuple[str, Dict[str, str]]]:
    """(device id, //#device tags) of a header that carries its own //#device record, else None."""
    for record in read_records(path):
        if record["kind"] == "device":
            name, tags, _ = parse_record(record)
            return name, tags
    return None


def check_class_pin_refs(path: Path, cfg_body: str, cfg_type: str) -> None:
    """Every sys_io_pin_ref_t field of the cfg must be in the class's SYS_DEVICE_PINS table
    (`offsetof(<cfg_type>, field)` in the device's .c files), and nothing else: sys_device_create
    checks pin order only for the listed offsets, so a forgotten field would go unchecked."""
    declared = set(re.findall(rf"\b{PIN_REF_TYPE}\s+(\w+)\s*;", cfg_body))
    listed = set()
    for source in sorted(path.parent.parent.glob("*.c")):
        listed.update(re.findall(rf"offsetof\(\s*{cfg_type}\s*,\s*(\w+)\s*\)", source.read_text(encoding="utf-8", errors="ignore")))
    if declared != listed:
        sys.exit(f"ERROR: {path.name}: pin refs of {cfg_type} {sorted(declared)} differ from the class's SYS_DEVICE_PINS table {sorted(listed)}"
                 f" (missing: {sorted(declared - listed)}, unknown: {sorted(listed - declared)})")


def parse_device_header_packet(path: Path, all_classes: Dict[str, str], defines, sdkconfig, symbols) -> Dict[str, dict]:
    """The create packet of a device that keeps everything in its own header: the packed
    `d_<chip>_cfg_t` struct, the `@type-id` of its //#device record as the packet header byte,
    class 0x01 (create frames). The packet is named packet_sys_device_install_<chip>_t, as
    the decoder headers name theirs, so the descriptor side doesn't care where it came from."""
    record = device_header_record(path)
    if record is None:
        return {}
    device_id, tags = record
    text = path.read_text(encoding="utf-8", errors="ignore")
    match = DEVICE_CFG_RE.search(text)
    if not match:
        sys.exit(f"ERROR: {path.name}: //#device {device_id} needs a `typedef struct __packed {{ ... }} d_<chip>_cfg_t;` (its create frame)")
    if "type_id" not in tags:
        sys.exit(f"ERROR: {path.name}: //#device {device_id} is missing @type-id (the byte after 0x00 in its create frame)")
    type_id, shown = resolve_numeric(tags["type_id"], defines, sdkconfig)
    if type_id is None:
        sys.exit(f"ERROR: {path.name}: @type-id {shown}")
    if CREATE_FRAME_CLASS not in all_classes:
        sys.exit(f"ERROR: no packet class {CREATE_FRAME_CLASS} for the create frame")
    check_class_pin_refs(path, match.group(1), match.group(2))
    chip = device_id.removeprefix("device_")
    packet = f"packet_sys_device_install_{chip}_t"
    fields = parse_struct_fields(match.group(1), defines, sdkconfig)
    entry = build_packet_entry(packet, f"0x{type_id:02X}", fields, path.name, CREATE_FRAME_CLASS, all_classes[CREATE_FRAME_CLASS], defines, sdkconfig, symbols)
    entry["decoder"] = f"sys_device_create(&g_{path.stem.removeprefix('device_')}_class)"
    return {packet: entry}


def read_records(path: Path) -> List[dict]:
    """The //#<kind> records of one header, each with its continuation lines, in file order."""
    records: List[dict] = []
    current: Optional[dict] = None
    for number, line in enumerate(path.read_text(encoding="utf-8", errors="ignore").splitlines(), 1):
        legacy = LEGACY_DIRECTIVE_RE.match(line)
        if legacy:
            sys.exit(f"ERROR: {path.name}:{number}: //@{legacy.group(1)} is the old one-line form - use the //#device / //#contract records (device-annotations.md)")
        record = RECORD_RE.match(line)
        if record:
            if record.group("kind") not in RECORD_TAGS:
                sys.exit(f"ERROR: {path.name}:{number}: unknown record //#{record.group('kind')} (known: {', '.join(RECORD_TAGS)})")
            current = {"kind": record.group("kind"), "head": record.group("rest") or "", "lines": [], "where": f"{path.name}:{number}"}
            records.append(current)
            continue
        continuation = CONTINUATION_RE.match(line)
        if continuation and current is not None:
            current["lines"].append(continuation.group("text"))
            continue
        current = None
    return records


def parse_record(record: dict) -> Tuple[str, Dict[str, str], List[dict]]:
    """A record's name, its tags and (contracts only) its @param lines. A line whose first token is
    not a tag continues the text of the tag before it."""
    kind, where = record["kind"], record["where"]
    name, _, inline = record["head"].partition(" ")
    if not name or name.startswith("@"):
        sys.exit(f"ERROR: {where}: //#{kind} needs a name")
    tags: Dict[str, str] = {}
    params: List[dict] = []
    last: Optional[Tuple[Dict[str, str], str]] = None
    for text in [inline.strip(), *record["lines"]]:
        if not text:
            continue
        if not text.startswith("@"):
            if last is None:
                sys.exit(f"ERROR: {where}: text '{text}' does not follow a tag")
            target, key = last
            target[key] = f"{target[key]} {text}".strip()
            continue
        if re.match(r"@param\b", text):
            if kind != "contract":
                sys.exit(f"ERROR: {where}: @param belongs in a //#contract record")
            param_name, _, annotation = text[len("@param"):].strip().partition(" ")
            param_tags = parse_tags(annotation)
            if not param_name or "desc" in param_tags:
                sys.exit(f"ERROR: {where}: '@param {param_name}' needs a field name followed by tags")
            unknown = sorted(set(param_tags) - PARAM_TAGS)
            if unknown:
                sys.exit(f"ERROR: {where}: @param {param_name}: unsupported tag(s) {unknown}; allowed: {sorted(PARAM_TAGS)}")
            params.append({"name": param_name, "tags": param_tags})
            last = (param_tags, list(param_tags)[-1]) if param_tags else None
            continue
        parsed = parse_tags(text)
        unknown = sorted(set(parsed) - RECORD_TAGS[kind])
        if unknown:
            sys.exit(f"ERROR: {where}: //#{kind} {name}: unsupported tag(s) {unknown}; allowed: {sorted(RECORD_TAGS[kind])}")
        for key, value in parsed.items():
            if key in tags:
                sys.exit(f"ERROR: {where}: //#{kind} {name}: @{key} given twice")
            tags[key] = value
        last = (tags, list(parsed)[-1])
    return name, tags, params


def parse_property(kind: str, name: str, tags: Dict[str, str], symbols: Dict[str, dict], defines: Dict[str, str], sdkconfig: Dict[str, int], ctx: str) -> dict:
    one_of = parse_choice_list(tags["one_of"], symbols, ctx) if "one_of" in tags else []
    enum_ref = tags.get("enum_ref")
    if kind == "property" and not enum_ref:
        enum_names = {v["enum"] for v in one_of if isinstance(v, dict)}
        if len(enum_names) > 1:
            sys.exit(f"ERROR: {ctx}: property '{name}' mixes symbols from different enums ({enum_names}) - enum-ref would be ambiguous")
        enum_ref = next(iter(enum_names), None)
    prop = {"one_of": one_of, "enum_ref": enum_ref}
    default = tags.get("default")
    if default is not None:
        resolved = resolve_symbol(default, symbols, ctx)["value"] if default.startswith("$") else resolve_numeric(default, defines, sdkconfig)[0]
        if resolved is None or resolved not in [choice["value"] if isinstance(choice, dict) else choice for choice in one_of]:
            sys.exit(f"ERROR: {ctx}: @default must name a value in @one-of")
        prop["default"] = resolved
    for key in ("alias", "note"):
        if tags.get(key):
            prop[key] = tags[key]
    return prop


def parse_parameter(param: dict, registry: Dict[str, dict], symbols: Dict[str, dict], defines: Dict[str, str], sdkconfig: Dict[str, int], ctx: str) -> dict:
    tags = param["tags"]
    parameter: dict = {"name": param["name"]}
    arg = tags.get("arg")
    if arg:
        if arg not in registry:
            sys.exit(f"ERROR: {ctx}: @arg '{arg}' does not match any //#self-property or //#property in this file")
        prop = registry[arg]
        parameter["one_of"] = prop["one_of"]
        if prop["enum_ref"]:
            parameter["enum_ref"] = prop["enum_ref"]
        # The property's label, note and default hold for every parameter using it; the parameter's own tags win.
        for key in ("default", "alias", "note"):
            if key in prop:
                parameter[key] = prop[key]
    if "one_of" in tags:
        parameter["one_of"] = parse_choice_list(tags["one_of"], symbols, ctx)
    if "device_wide" in tags:
        # The field selects nothing: the call applies to the whole device and the field is sent as 0.
        parameter["device_wide"] = True
    for tag in ("min", "max", "default"):
        if tag in tags:
            resolved, shown = (resolve_symbol(tags[tag], symbols, ctx)["value"], tags[tag]) if tag == "default" and tags[tag].startswith("$") else resolve_numeric(tags[tag], defines, sdkconfig)
            if resolved is None:
                sys.exit(f"ERROR: {ctx}: @{tag} {shown}")
            parameter[tag] = resolved
    for tag in ("alias", "type", "unit", "note"):
        if tag in tags:
            parameter[tag] = tags[tag]
    return parameter


def parse_device_descriptor(path: Path, symbols: Dict[str, dict], defines: Dict[str, str], sdkconfig: Dict[str, int]) -> Optional[dict]:
    records = [(record, *parse_record(record)) for record in read_records(path)]
    if not records:
        return None
    devices = [entry for entry in records if entry[0]["kind"] == "device"]
    if len(devices) != 1:
        sys.exit(f"ERROR: {path.name}: expected exactly one //#device record, found {len(devices)}")
    _, device_id, metadata, _ = devices[0]
    missing = [key for key in ("title", "description") if not metadata.get(key)]
    if missing:
        sys.exit(f"ERROR: {devices[0][0]['where']}: //#device {device_id} is missing @{', @'.join(missing)}")
    provider = metadata.get("contract_provider")
    ctx = devices[0][0]["where"]
    contract_provider = resolve_symbol(provider, symbols, ctx) if provider and provider.startswith("$") else provider

    registry: Dict[str, dict] = {}
    for record, name, tags, _ in records:
        if record["kind"] in ("self-property", "property"):
            if name in registry:
                sys.exit(f"ERROR: {record['where']}: property '{name}' defined twice")
            registry[name] = parse_property(record["kind"], name, tags, symbols, defines, sdkconfig, record["where"])

    contracts: List[dict] = []
    for record, name, tags, params in records:
        if record["kind"] != "contract":
            continue
        contract: dict = {"packet": name, "parameters": [parse_parameter(param, registry, symbols, defines, sdkconfig, f"{record['where']}: @param {param['name']}") for param in params]}
        for key in ("alias", "description", "returns"):
            if tags.get(key):
                contract[key] = tags[key]
        contracts.append(contract)

    return {
        "metadata": {"id": device_id, **metadata},
        "contract_provider": contract_provider,
        "contracts": contracts,
        "source_file": path.name,
    }


def pwm_frequencies(metadata: Dict[str, str], defines: Dict[str, str], sdkconfig: Dict[str, int], source: str) -> Optional[int]:
    """@pwm-frequencies <value> [@count-bits]: how many different PWM frequencies the device runs at once."""
    raw = metadata.get("pwm_frequencies")
    if raw is None:
        return None
    value, shown = resolve_numeric(raw, defines, sdkconfig)
    if value is None:
        sys.exit(f"ERROR: {source}: @pwm-frequencies {shown}")
    return bin(value).count("1") if "count_bits" in metadata else value


def build_device_document(device: dict, packets: Dict[str, dict], defines: Dict[str, str], sdkconfig: Dict[str, int]) -> dict:
    metadata = device["metadata"]
    install_packets = [n for n, p in packets.items() if p["source_file"] == device["source_file"] and n.startswith("packet_sys_device_install_")]
    if len(install_packets) != 1:
        sys.exit(f"ERROR: {device['source_file']}: expected exactly one install packet, found {install_packets}")
    address = packets[install_packets[0]]["fields"].get("i2c_addr")
    if address is not None and not address.get("one_of"):
        sys.exit(f"ERROR: {device['source_file']}: install i2c_addr must declare @one-of with the device's available addresses")

    contract_documents = []
    for contract in device["contracts"]:
        packet_name = contract["packet"]
        if packet_name not in packets:
            sys.exit(f"ERROR: {device['source_file']}: @contract references unknown packet {packet_name}")
        packet_def = packets[packet_name]
        parameters = list(contract["parameters"])
        covered = {p["name"] for p in parameters}
        # device_id addresses "which installed device instance" - supplied by the calling
        # context on every contract packet, not something a device author annotates. Synthesize
        # it as an explicit, marked parameter (instead of silently omitting it) so a client can
        # tell "auto-fill from context" apart from "actually missing" just by reading parameters.
        if "device_id" in packet_def["fields"] and "device_id" not in covered:
            parameters.insert(0, {"name": "device_id", "alias": packet_def["fields"]["device_id"].get("alias", "Device ID"), "instance": True})
            covered.add("device_id")
        missing_required = [fname for fname, fdef in packet_def["fields"].items() if fdef.get("required") and fname not in covered]
        if missing_required:
            sys.exit(
                f"ERROR: {device['source_file']}: contract '{packet_name}' is missing required field(s) {missing_required} "
                f"in its @param list - every @required field on the generic packet must be exposed (or explicitly given a fixed value)"
            )
        contract_documents.append({**contract, "parameters": parameters, "packet_definition": packet_def})

    return {
        "$schema": SCHEMA_REL_PATH,
        "schemaVersion": 1,
        "kind": "device-definition",
        "id": metadata["id"],
        "title": metadata["title"],
        "description": metadata["description"],
        "protocols": metadata.get("protocol", "").split(),
        "tags": metadata.get("tags", "").split(),
        **({"datasheet": metadata["datasheet"]} if metadata.get("datasheet") else {}),
        **({"pwm_frequencies": frequencies} if (frequencies := pwm_frequencies(metadata, defines, sdkconfig, device["source_file"])) is not None else {}),
        "source_file": device["source_file"],
        "contractProvider": device["contract_provider"],
        "install": {"packet": install_packets[0], "packet_definition": packets[install_packets[0]]},
        "contracts": contract_documents,
    }


def validate_document(device: dict) -> None:
    """Hard error on a schema violation - same policy as an unresolved $SYMBOL:
    a generated file that doesn't match the shape it claims ($schema) is a bug
    in the generator or the annotation grammar, not something to write anyway."""
    if jsonschema is None:
        print("  WARNING: jsonschema package not installed - skipping schema validation (pip install jsonschema)")
        return
    schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
    validator = jsonschema.Draft202012Validator(schema)
    errors = sorted(validator.iter_errors(device), key=lambda e: list(e.path))
    if errors:
        details = "\n".join(f"  - at {'/'.join(str(p) for p in e.path) or '<root>'}: {e.message}" for e in errors)
        sys.exit(f"ERROR: {device['id']} failed schema validation against {SCHEMA_PATH.name}:\n{details}")


def scan_context() -> Tuple[Dict[str, str], Dict[str, dict], Dict[str, str], Dict[str, int]]:
    """Packet classes, published enum symbols, #defines and sdkconfig values the headers resolve against."""
    all_classes = scan_all_class_headers()
    all_classes.update(scan_kconfig_class_headers())
    symbols = generate_enums.scan(generate_enums.COMPONENTS_DIR)["symbols"]
    defines = scan_defines(PROJECT_ROOT / "components")
    sdkconfig = scan_sdkconfig(SDKCONFIG_PATH)
    if not sdkconfig:
        print(f"  WARNING: no sdkconfig found at {SDKCONFIG_PATH} - CONFIG_* symbols in @min/@max will stay unresolved")
    return all_classes, symbols, defines, sdkconfig


def find_device_headers(devices_root: Path) -> List[Path]:
    """Headers of components/devices/device_*/include that describe their own device (//#device)."""
    return sorted(p for p in devices_root.glob("device_*/include/device_*.h") if device_header_record(p) is not None)


def build_devices(header_files: List[Path], context: Tuple[Dict[str, str], Dict[str, dict], Dict[str, str], Dict[str, int]], device_headers: List[Path] = ()) -> List[dict]:
    """One device document per annotated header: the dec_device_*.h decoder headers under a `device/`
    folder, and the device headers that carry their own //#device record. Packets from every header."""
    all_classes, symbols, defines, sdkconfig = context
    packets: Dict[str, dict] = {}
    for path in header_files:
        packets.update(parse_packet_file(path, all_classes, defines, sdkconfig, symbols))
    for path in device_headers:
        packets.update(parse_device_header_packet(path, all_classes, defines, sdkconfig, symbols))
    devices = []
    for path in [*(p for p in header_files if p.parent.name == "device"), *device_headers]:
        descriptor = parse_device_descriptor(path, symbols, defines, sdkconfig)
        if descriptor:
            devices.append(build_device_document(descriptor, packets, defines, sdkconfig))
    return devices


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate one self-contained device JSON per annotated dec_device_*.h.")
    parser.add_argument("root_folder", help="Decoders root to scan (e.g. components/codecs/decoders)")
    parser.add_argument("target_dir", help="Output directory - one <device-id>.generated.json per device, named automatically")
    args = parser.parse_args()

    decoders_dir = Path(args.root_folder)
    if not decoders_dir.exists():
        print(f"Decoders directory not found: {decoders_dir}")
        return 1

    header_files = sorted(p for p in decoders_dir.rglob("dec_*.h") if p.name != "dec_device_common.h")
    if not header_files:
        print(f"No dec_*.h files found in {decoders_dir}")
        return 1

    print("Resolving classes, enums, defines, sdkconfig...")
    context = scan_context()
    symbols = context[1]
    devices = build_devices(header_files, context, find_device_headers(decoders_dir.resolve().parents[1] / "devices"))

    target_dir = Path(args.target_dir)
    target_dir.mkdir(parents=True, exist_ok=True)
    for device in devices:
        validate_document(device)
        out_path = target_dir / f"{device['id']}.generated.json"
        out_path.write_text(json.dumps(device, indent=2) + "\n", encoding="utf-8")
        print(f"Wrote {out_path}")

    print(f"  Device headers scanned : {sum(p.parent.name == 'device' for p in header_files)} decoder + {len(find_device_headers(decoders_dir.resolve().parents[1] / 'devices'))} own")
    print(f"  Device descriptors     : {len(devices)}")
    print(f"  Symbols available      : {len(symbols)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
