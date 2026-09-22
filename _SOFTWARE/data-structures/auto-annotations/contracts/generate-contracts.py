"""Generate the catalog of packet contracts explicitly exposed by decoder C headers.

Only headers marked with //@contract-catalog and //@contract-list contribute.
The list annotation is intentionally tied to the decoder X-macro, so packet
declarations which are private implementation details never leak into client
metadata. Packet fields and enum references reuse the existing device parser;
this keeps one C field-annotation grammar and one class-byte resolver.
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
DECODERS_DIR = PROJECT_ROOT / "components" / "codecs" / "decoders"
OUT_PATH = PROJECT_ROOT / "data-structures" / "contracts" / "contracts.generated.json"
SCHEMA_PATH = PROJECT_ROOT / "data-structures" / "schema" / "contracts.schema.json"
SCHEMA_REL_PATH = SCHEMA_PATH.relative_to(PROJECT_ROOT).as_posix()

DEVICE_GENERATOR_PATH = PROJECT_ROOT / "data-structures" / "auto-annotations" / "device" / "generate-devices.py"
spec = importlib.util.spec_from_file_location("generate_devices", DEVICE_GENERATOR_PATH)
device = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = device
spec.loader.exec_module(device)

CATALOG_RE = re.compile(r"^\s*//@contract-catalog\s+(?P<id>[\w-]+)(?P<tags>.*)$", re.MULTILINE)
LIST_RE = re.compile(r"^\s*//@contract-list\s+(?P<name>\w+)\s*$", re.MULTILINE)
LIST_DEFINE_RE = r"#define\s+{name}\s*\(X\)(?P<body>.*?)(?=\n\s*#define|\Z)"
LIST_ITEM_RE = re.compile(r"X\(\s*HEADER_(packet_\w+_t)\s*,\s*(packet_\w+_t)\s*,\s*(decoder_\w+)\s*\)")


def fail(message: str) -> None:
    raise SystemExit(f"ERROR: {message}")


def parse_catalog(path: Path) -> tuple[str, str, str, str]:
    text = path.read_text(encoding="utf-8", errors="ignore")
    catalog = CATALOG_RE.search(text)
    contract_list = LIST_RE.search(text)
    if not catalog and not contract_list:
        return None
    if not catalog or not contract_list:
        fail(f"{path}: //@contract-catalog and //@contract-list must appear together")
    tags = device.parse_tags(catalog.group("tags"))
    title = tags.get("title")
    description = tags.get("description")
    if not title or not description:
        fail(f"{path}: //@contract-catalog requires @title and @description")
    return catalog.group("id"), title, description, contract_list.group("name")


def listed_packets(text: str, list_name: str, path: Path) -> list[tuple[str, str]]:
    macro = re.search(LIST_DEFINE_RE.format(name=re.escape(list_name)), text, re.DOTALL)
    if not macro:
        fail(f"{path}: //@contract-list names unknown macro {list_name}")
    items = [(packet, decoder) for header, packet, decoder in LIST_ITEM_RE.findall(macro.group("body")) if header == packet]
    if not items:
        fail(f"{path}: {list_name} contains no packet X(...) entries")
    duplicates = {packet for packet, _ in items if sum(packet == candidate for candidate, _ in items) > 1}
    if duplicates:
        fail(f"{path}: {list_name} exports duplicate packet(s): {sorted(duplicates)}")
    return items


def response_layout(packet_name: str, structs: dict, path: Path, defines, sdkconfig, symbols):
    """OK-status data of a command: packet_<name>_response_t, if the decoder declares one."""
    response_name = packet_name[: -len("_t")] + "_response_t"
    if response_name not in structs:
        return None
    fields = device.parse_struct_fields(structs[response_name], defines, sdkconfig)
    entry = device.build_packet_entry(response_name, "0x00", fields, path.name, None, None, defines, sdkconfig, symbols)
    return {"struct": response_name, "field_order": entry["field_order"], "fields": entry["fields"]}


def response_stream(class_byte: int) -> dict:
    """Envelope of every command response (sys_interface.h, sys_interface_status_e)."""
    return {
        "class_header": f"0x{class_byte:02X}",
        "header": ["request_class", "request_packet", "status"],
        "status_enum": "sys_interface_status_e",
        "matching": "fifo",
        "error_data": {"field_order": ["tag", "owner"], "fields": {"tag": {"type": "uint16_t"}, "owner": {"type": "uint16_t"}}},
    }


def build_document() -> dict:
    all_classes = device.scan_all_class_headers()
    all_classes.update(device.scan_kconfig_class_headers())
    symbols = device.generate_enums.scan(device.generate_enums.COMPONENTS_DIR)["symbols"]
    defines = device.scan_defines(PROJECT_ROOT / "components")
    sdkconfig = device.scan_sdkconfig(PROJECT_ROOT / "sdkconfig")
    catalogs = []
    for path in sorted(DECODERS_DIR.rglob("dec_*.h")):
        metadata = parse_catalog(path)
        if metadata is None:
            continue
        catalog_id, title, description, list_name = metadata
        class_info = device.resolve_class(path.read_text(encoding="utf-8", errors="ignore"), all_classes)
        if not class_info:
            fail(f"{path}: exposed contract catalog has no resolvable packet class")
        packets = device.parse_packet_file(path, all_classes, defines, sdkconfig, symbols)
        text = path.read_text(encoding="utf-8", errors="ignore")
        structs = {m.group(2): m.group(1) for m in device.STRUCT_RE.finditer(text)}
        listed = listed_packets(text, list_name, path)
        contracts = []
        for packet_name, decoder in listed:
            if packet_name not in packets:
                fail(f"{path}: {list_name} exports {packet_name}, but no matching HEADER/packed struct exists")
            packet = packets[packet_name]
            response = response_layout(packet_name, structs, path, defines, sdkconfig, symbols)
            contracts.append({
                "id": packet_name,
                "packet": packet_name,
                "packet_header": packet["packet_header"],
                "decoder": f"{decoder}()",
                "field_order": packet["field_order"],
                "fields": packet["fields"],
                **({"groups": packet["groups"]} if "groups" in packet else {}),
                **({"response": response} if response else {}),
            })
        class_name, class_header = class_info
        catalogs.append({
            "id": catalog_id,
            "title": title,
            "description": description,
            "source_file": path.relative_to(PROJECT_ROOT).as_posix(),
            "class_name": class_name,
            "class_header": class_header,
            "contracts": contracts,
        })
    if not catalogs:
        fail(f"no annotated contract catalogs found under {DECODERS_DIR}")
    ids = [catalog["id"] for catalog in catalogs]
    if len(ids) != len(set(ids)):
        fail(f"duplicate contract catalog id(s): {sorted(identifier for identifier in set(ids) if ids.count(identifier) > 1)}")
    stream = sdkconfig.get("CONFIG_TX_PACKET_CLASS_INTERFACE")
    if stream is None:
        fail("CONFIG_TX_PACKET_CLASS_INTERFACE not found in sdkconfig (response stream class byte)")
    return {"$schema": SCHEMA_REL_PATH, "schemaVersion": 1, "kind": "contracts", "response_stream": response_stream(stream), "catalogs": catalogs}


def validate(document: dict) -> None:
    if jsonschema is None:
        print("WARNING: jsonschema package not installed - skipping schema validation")
        return
    schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
    errors = sorted(jsonschema.Draft202012Validator(schema).iter_errors(document), key=lambda error: list(error.path))
    if errors:
        details = "\n".join(f"  - at {'/'.join(map(str, error.path)) or '<root>'}: {error.message}" for error in errors)
        fail(f"contracts failed schema validation:\n{details}")


def main() -> int:
    document = build_document()
    validate(document)
    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUT_PATH.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    total = sum(len(catalog["contracts"]) for catalog in document["catalogs"])
    print(f"Wrote {OUT_PATH.relative_to(PROJECT_ROOT)} ({len(document['catalogs'])} catalogs, {total} contracts)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
