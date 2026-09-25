"""Generate the board catalog: the onboard devices and their fixed device IDs.

Source of truth:
- components/runit/runit_board_defs.h: `#define DEVICE_ID_<NAME> <id>  //@STATIC_DEVICE`
- components/runit/runit_board_cfg.c: `RUNIT_BOARD_DEVICE(DEVICE_ID_<NAME>, d_<driver>_create(...))`,
  which names the driver; when data-structures/devices/device_<driver>.generated.json
  exists, the device links to that descriptor and takes its title.

The app names a device ID (in error payloads, events, pin references) from
this file instead of showing a bare number.

Usage:
  python data-structures/auto-annotations/board/generate-board.py
"""
import json
import re
import sys
from pathlib import Path

try:
    import jsonschema
except ImportError:
    jsonschema = None

PROJECT_ROOT = Path(__file__).parents[3]
DEFS_PATH = PROJECT_ROOT / "components" / "runit" / "runit_board_defs.h"
CFG_PATH = PROJECT_ROOT / "components" / "runit" / "runit_board_cfg.c"
DEVICES_DIR = PROJECT_ROOT / "data-structures" / "devices"
OUT_PATH = PROJECT_ROOT / "data-structures" / "board" / "board.generated.json"
SCHEMA_PATH = PROJECT_ROOT / "data-structures" / "schema" / "board.schema.json"
SCHEMA_REL_PATH = SCHEMA_PATH.relative_to(PROJECT_ROOT).as_posix()

STATIC_DEVICE_RE = re.compile(r"^\s*#define\s+(?P<symbol>DEVICE_ID_(?P<name>\w+))\s+(?P<id>\d+)\s*//@STATIC_DEVICE\b", re.MULTILINE)
CREATE_RE = re.compile(r"RUNIT_BOARD_DEVICE\(\s*(?P<symbol>DEVICE_ID_\w+)\s*,\s*d_(?P<driver>\w+?)_create\s*\(")


def fail(message: str) -> None:
    raise SystemExit(f"ERROR: {message}")


def build_document() -> dict:
    defs = DEFS_PATH.read_text(encoding="utf-8", errors="replace")
    drivers = {match.group("symbol"): match.group("driver") for match in CREATE_RE.finditer(CFG_PATH.read_text(encoding="utf-8", errors="replace"))}
    # Descriptor files don't always split the part number the way the driver does (device_ads_7128 vs d_ads7128).
    descriptors = {path.name.removesuffix(".generated.json").replace("_", ""): path for path in DEVICES_DIR.glob("device_*.generated.json")}
    devices = []
    for match in STATIC_DEVICE_RE.finditer(defs):
        symbol, device_id = match.group("symbol"), int(match.group("id"))
        driver = drivers.get(symbol)
        entry = {"id": device_id, "symbol": symbol, "name": match.group("name")}
        if driver:
            entry["driver"] = driver
            descriptor_path = descriptors.get(f"device{driver}".replace("_", ""))
            if descriptor_path:
                descriptor = json.loads(descriptor_path.read_text(encoding="utf-8"))
                entry["descriptor"] = descriptor["id"]
                entry["title"] = descriptor["title"]
        devices.append(entry)
    if not devices:
        fail(f"{DEFS_PATH.name}: no //@STATIC_DEVICE defines")
    for key in ("id", "symbol"):
        values = [device[key] for device in devices]
        if len(values) != len(set(values)):
            fail(f"two static devices share a {key}: {values}")
    unknown = sorted(set(drivers) - {device["symbol"] for device in devices})
    if unknown:
        fail(f"{CFG_PATH.name} creates {unknown}, which have no //@STATIC_DEVICE define")
    return {
        "$schema": SCHEMA_REL_PATH,
        "schemaVersion": 1,
        "kind": "board",
        "source_files": [DEFS_PATH.relative_to(PROJECT_ROOT).as_posix(), CFG_PATH.relative_to(PROJECT_ROOT).as_posix()],
        "devices": sorted(devices, key=lambda device: device["id"]),
    }


def validate(document: dict) -> None:
    if jsonschema is None:
        print("WARNING: jsonschema package not installed - skipping schema validation")
        return
    schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
    errors = sorted(jsonschema.Draft202012Validator(schema).iter_errors(document), key=lambda error: list(error.path))
    if errors:
        details = "\n".join(f"  - at {'/'.join(map(str, error.path)) or '<root>'}: {error.message}" for error in errors)
        fail(f"board failed schema validation:\n{details}")


def main() -> int:
    document = build_document()
    validate(document)
    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUT_PATH.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {OUT_PATH.relative_to(PROJECT_ROOT)} ({len(document['devices'])} static devices)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
