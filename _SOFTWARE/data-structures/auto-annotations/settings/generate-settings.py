"""Generate annotated runtime-settings packet catalogs from decoder headers."""
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
OUT_PATH = PROJECT_ROOT / "data-structures" / "settings" / "settings.generated.json"
SCHEMA_PATH = PROJECT_ROOT / "data-structures" / "schema" / "settings.schema.json"
SCHEMA_REL_PATH = SCHEMA_PATH.relative_to(PROJECT_ROOT).as_posix()

DEVICE_GENERATOR_PATH = PROJECT_ROOT / "data-structures" / "auto-annotations" / "device" / "generate-devices.py"
spec = importlib.util.spec_from_file_location("generate_devices", DEVICE_GENERATOR_PATH)
device = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = device
spec.loader.exec_module(device)

SETTINGS_RE = re.compile(r"^\s*//@settings\s+(?P<tag>[\w-]+)(?P<tags>.*)$", re.MULTILINE)
SETTINGS_CLASS_RE = re.compile(r"^\s*//@settings-class\s+(?P<name>\w+)\s*$", re.MULTILINE)


def fail(message: str) -> None:
    raise SystemExit(f"ERROR: {message}")


def build_document() -> dict:
    all_classes = device.scan_all_class_headers()
    all_classes.update(device.scan_kconfig_class_headers())
    symbols = device.generate_enums.scan(device.generate_enums.COMPONENTS_DIR)["symbols"]
    defines = device.scan_defines(PROJECT_ROOT / "components")
    sdkconfig = device.scan_sdkconfig(PROJECT_ROOT / "sdkconfig")
    settings = []

    for path in sorted(DECODERS_DIR.glob("dec_settings_*.h")):
        text = path.read_text(encoding="utf-8", errors="ignore")
        directive = SETTINGS_RE.search(text)
        if not directive:
            continue
        tags = device.parse_tags(directive.group("tags"))
        if not tags.get("title") or not tags.get("description"):
            fail(f"{path}: //@settings requires @title and @description")
        class_directive = SETTINGS_CLASS_RE.search(text)
        class_info = (class_directive.group("name"), all_classes.get(class_directive.group("name"))) if class_directive else device.resolve_class(text, all_classes)
        if class_info and class_info[1] is None:
            fail(f"{path}: //@settings-class names unknown class {class_info[0]}")
        if not class_info:
            fail(f"{path}: settings decoder requires //@settings-class")
        packets = device.parse_packet_file(path, all_classes, defines, sdkconfig, symbols)
        if not packets:
            fail(f"{path}: settings decoder has no packet structs")
        class_name, class_header = class_info
        settings.append({
            "tag": directive.group("tag"),
            "title": tags["title"],
            "description": tags["description"],
            "source_file": path.relative_to(PROJECT_ROOT).as_posix(),
            "class_name": class_name,
            "class_header": class_header,
            "packets": [{
                "id": name,
                "packet": name,
                "packet_header": packet["packet_header"],
                "field_order": packet["field_order"],
                "fields": packet["fields"],
                **({"groups": packet["groups"]} if "groups" in packet else {}),
            } for name, packet in packets.items()],
        })

    if not settings:
        fail(f"no annotated settings decoders found under {DECODERS_DIR}")
    tags = [setting["tag"] for setting in settings]
    if len(tags) != len(set(tags)):
        fail(f"duplicate settings tag(s): {sorted(tag for tag in set(tags) if tags.count(tag) > 1)}")
    return {"$schema": SCHEMA_REL_PATH, "schemaVersion": 1, "kind": "settings", "settings": settings}


def main() -> int:
    document = build_document()
    if jsonschema is None:
        print("WARNING: jsonschema package not installed - skipping schema validation")
    else:
        schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
        errors = sorted(jsonschema.Draft202012Validator(schema).iter_errors(document), key=lambda error: list(error.path))
        if errors:
            fail("settings failed schema validation:\n" + "\n".join(f"  - at {'/'.join(map(str, error.path)) or '<root>'}: {error.message}" for error in errors))
    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUT_PATH.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    total = sum(len(entry["packets"]) for entry in document["settings"])
    print(f"Wrote {OUT_PATH.relative_to(PROJECT_ROOT)} ({len(document['settings'])} settings catalogs, {total} packets)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
