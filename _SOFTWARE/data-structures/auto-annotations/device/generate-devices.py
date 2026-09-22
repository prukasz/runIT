"""
Device JSON generator for components/codecs/decoders/**/dec_device_*.h.

Reads device-level //@ directives (id/version/title/description/protocol/tags/
contract-provider/self-property/property/contract/param/returns) plus the
field-level @tag comments already used across every dec_*.h packet struct, and
renders one self-contained JSON descriptor per annotated device header. See
data-structures/auto-annotations/device/device-annotations.md for the annotation grammar.

Design notes:
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
  - Doesn't touch sync_decoders_from_c.py; reuses its class-byte resolution
    (scan_all_class_headers / scan_kconfig_class_headers / resolve_class).
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
sys.path.insert(0, str(PROJECT_ROOT / "Python" / "PythonRunIT"))
sys.path.insert(0, str(Path(__file__).parents[1] / "enums"))
from sync_decoders_from_c import scan_all_class_headers, scan_kconfig_class_headers, resolve_class  # reuse, don't duplicate

import importlib.util as _ilu  # generate-enums.py has a hyphenated filename, so import it by path instead of by module name
_spec = _ilu.spec_from_file_location("generate_enums", Path(__file__).parents[1] / "enums" / "generate-enums.py")
generate_enums = _ilu.module_from_spec(_spec)
_spec.loader.exec_module(generate_enums)

SDKCONFIG_PATH = PROJECT_ROOT / "sdkconfig"

PACKET_HEADER_RE = re.compile(r"#define\s+HEADER_(packet_\w+)\s+(0x[0-9A-Fa-f]+)")
STRUCT_RE = re.compile(r"typedef\s+struct\s+(?:__packed\s*)?\{(.*?)\}\s*(packet_\w+_t)\s*;", re.DOTALL)
FIELD_LINE_RE = re.compile(
    r"^\s*(?P<type>[A-Za-z_][\w ]*?)\s+(?P<name>\w+)\s*(?:\[\s*(?P<arr>\d+)\s*\])?\s*;\s*(?://\s*(?P<comment>.*))?\s*$"
)
TAG_RE = re.compile(r"@([\w-]+)\b")
DEFINE_RE = re.compile(r"#define\s+([A-Z][A-Z0-9_]*)\s+([^\s/][^\n/]*)")
SDKCONFIG_RE = re.compile(r"^(CONFIG_\w+)=(.+)$", re.MULTILINE)
DEVICE_DIRECTIVE_RE = re.compile(r"^\s*//@(?P<name>[\w-]+)(?:\s+(?P<value>.*))?\s*$")


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
    tags: Dict[str, str]


def parse_struct_fields(body: str) -> List[Field]:
    fields = []
    for raw_line in body.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        m = FIELD_LINE_RE.match(line)
        if not m:
            continue
        arr = m.group("arr")
        fields.append(Field(m.group("type").strip(), m.group("name"), int(arr) if arr else None, parse_tags(m.group("comment") or "")))
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
        else:
            values.append(to_int(token) if to_int(token) is not None else token)
    return values


def field_json(f: Field, defines: Dict[str, str], sdkconfig: Dict[str, int], symbols: Dict[str, dict], context: str) -> dict:
    out: dict = {"type": f.type}
    if f.array_len:
        out["array_len"] = f.array_len
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
    if "available" in f.tags:
        out["available"] = parse_choice_list(f.tags["available"], symbols, context)
    if "default" in f.tags:
        out["default"] = resolve_numeric(f.tags["default"], defines, sdkconfig)[0]
    if "sentinel" in f.tags:
        out["sentinel"] = resolve_numeric(f.tags["sentinel"], defines, sdkconfig)[0]
    if "unit" in f.tags:
        out["unit"] = f.tags["unit"]
    if "ref" in f.tags:
        out["ref"] = f.tags["ref"]
    if "group" in f.tags:
        out["group"] = f.tags["group"]
    if "role" in f.tags:
        out["role"] = f.tags["role"]
    if f.tags.get("desc"):
        out["desc"] = f.tags["desc"]
    if "note" in f.tags:
        out["note"] = f.tags["note"]
    return out


def build_packet_entry(name: str, header_value: str, fields: List[Field], source_file: str, class_name, class_hex, defines, sdkconfig, symbols) -> dict:
    context = f"{source_file}: {name}"
    order = [f.name for f in fields]
    groups: Dict[str, List[str]] = {}
    for f in fields:
        g = f.tags.get("group")
        if g:
            groups.setdefault(g, []).append(f.name)
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
        entry["groups"] = {gname: {"fields": gfields, "sentinel_field": next((f.name for f in fields if f.name in gfields and "sentinel" in f.tags), None)} for gname, gfields in groups.items()}
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
        fields = parse_struct_fields(structs[struct_name])
        out[struct_name] = build_packet_entry(struct_name, hexval, fields, source_file, class_name, class_hex, defines, sdkconfig, symbols)
    return out


def parse_device_descriptor(path: Path, symbols: Dict[str, dict]) -> Optional[dict]:
    metadata: Dict[str, str] = {}
    self_properties: Dict[str, dict] = {}
    properties: Dict[str, dict] = {}
    contracts: List[dict] = []
    current_contract: Optional[dict] = None
    contract_provider = None

    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        match = DEVICE_DIRECTIVE_RE.match(line)
        if not match:
            continue
        name = match.group("name")
        value = (match.group("value") or "").strip()
        ctx = f"{path.name}: //@{name} {value}"

        if name == "contract-provider":
            contract_provider = resolve_symbol(value, symbols, ctx) if value.startswith("$") else value
            continue

        if name in ("self-property", "property"):
            prop_name, _, annotation = value.partition(" ")
            if not prop_name:
                sys.exit(f"ERROR: {ctx}: //@{name} requires a NAME")
            tags = parse_tags(annotation)
            one_of = parse_choice_list(tags["one_of"], symbols, ctx) if "one_of" in tags else []
            ref = tags.get("ref")
            if name == "property" and not ref:
                enum_names = {v["enum"] for v in one_of if isinstance(v, dict)}
                if len(enum_names) > 1:
                    sys.exit(f"ERROR: {ctx}: @property '{prop_name}' mixes symbols from different enums ({enum_names}) - ref would be ambiguous")
                ref = next(iter(enum_names), None)
            target = self_properties if name == "self-property" else properties
            target[prop_name] = {"one_of": one_of, "ref": ref}
            continue

        if name == "contract":
            packet_name, _, annotation = value.partition(" ")
            if not packet_name:
                sys.exit(f"ERROR: {ctx}: @contract requires a packet name")
            current_contract = {"packet": packet_name, "parameters": []}
            for tag, tag_value in parse_tags(annotation).items():
                if tag == "alias":
                    current_contract["alias"] = tag_value
            contracts.append(current_contract)
            continue

        if current_contract is None:
            metadata[name] = value
            continue

        if name == "param":
            param_name, _, annotation = value.partition(" ")
            if not param_name:
                sys.exit(f"ERROR: {ctx}: @param requires a field name")
            tags = parse_tags(annotation)
            parameter: dict = {"name": param_name}
            arg = tags.get("arg")
            if arg:
                registry = {**self_properties, **properties}
                if arg not in registry:
                    sys.exit(f"ERROR: {ctx}: @arg '{arg}' does not match any //@self-property or //@property in this file")
                prop = registry[arg]
                parameter["one_of"] = prop["one_of"]
                if prop["ref"]:
                    parameter["ref"] = prop["ref"]
            if "one_of" in tags:
                parameter["one_of"] = parse_choice_list(tags["one_of"], symbols, ctx)
            if "available" in tags:
                parameter["available"] = parse_choice_list(tags["available"], symbols, ctx)
            if "optional" in tags:
                parameter["required"] = False
            for tag in ("min", "max", "default"):
                if tag in tags:
                    parameter[tag] = resolve_numeric(tags[tag], {}, {})[0] if to_int(tags[tag]) is not None else tags[tag]
            for tag in ("alias", "type", "unit"):
                if tag in tags:
                    parameter[tag] = tags[tag]
            current_contract["parameters"].append(parameter)
        elif name == "returns":
            current_contract["returns"] = value
        elif name == "description":
            current_contract["description"] = value

    if not metadata:
        return None
    missing = [key for key in ("id", "version", "title", "description") if not metadata.get(key)]
    if missing:
        sys.exit(f"ERROR: {path}: device metadata is missing //@{', //@'.join(missing)}")

    return {
        "metadata": metadata,
        "contract_provider": contract_provider,
        "contracts": contracts,
        "source_file": path.name,
    }


def build_device_document(device: dict, packets: Dict[str, dict]) -> dict:
    metadata = device["metadata"]
    install_packets = [n for n, p in packets.items() if p["source_file"] == device["source_file"] and n.startswith("packet_sys_device_install_")]
    if len(install_packets) != 1:
        sys.exit(f"ERROR: {device['source_file']}: expected exactly one install packet, found {install_packets}")

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
        "version": metadata["version"],
        "title": metadata["title"],
        "description": metadata["description"],
        "protocols": metadata.get("protocol", "").split(),
        "tags": metadata.get("tags", "").split(),
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
    all_classes = scan_all_class_headers()
    all_classes.update(scan_kconfig_class_headers())
    symbols = generate_enums.scan(generate_enums.COMPONENTS_DIR)["symbols"]
    defines = scan_defines(PROJECT_ROOT / "components")
    sdkconfig = scan_sdkconfig(SDKCONFIG_PATH)
    if not sdkconfig:
        print(f"  WARNING: no sdkconfig found at {SDKCONFIG_PATH} - CONFIG_* symbols in @min/@max will stay unresolved")

    packets: Dict[str, dict] = {}
    for path in header_files:
        packets.update(parse_packet_file(path, all_classes, defines, sdkconfig, symbols))

    devices = []
    for path in (p for p in header_files if p.parent.name == "device"):
        descriptor = parse_device_descriptor(path, symbols)
        if descriptor:
            devices.append(build_device_document(descriptor, packets))

    target_dir = Path(args.target_dir)
    target_dir.mkdir(parents=True, exist_ok=True)
    for device in devices:
        validate_document(device)
        out_path = target_dir / f"{device['id']}.generated.json"
        out_path.write_text(json.dumps(device, indent=2) + "\n", encoding="utf-8")
        print(f"Wrote {out_path}")

    print(f"  Device headers scanned : {sum(p.parent.name == 'device' for p in header_files)}")
    print(f"  Device descriptors     : {len(devices)}")
    print(f"  Symbols available      : {len(symbols)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
