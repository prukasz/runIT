"""Checks the parts of the hardware library (library/parts/<id>/part.json) against part.schema.json and their references.

  - the folder is the part's id, and its drawing (part.svg) is there with a viewBox of its size in mm
  - a device type is a published device descriptor; its preset is one of the descriptor's; a config key is a field of it
  - a pin's `config` is a pin-reference field of the part's device; `device_pin` 'self' needs a device
  - a pin's `net`, an H-bridge's `supply`, an element's `in` / `out` and a net's role match the part's nets
  - a board part names a board.json (name and revision)
  - ids of connectors, of the pins in a connector, of nets and of elements are unique

Needs the jsonschema package. Usage: python templates/check_library.py
"""
import json
import re

from _common import COMPONENTS_DIR, GENERATED, ROOT, fail, read, rel

try:
    from jsonschema import Draft202012Validator
except ImportError:
    fail("the jsonschema package is missing: pip install jsonschema")

LIBRARY = ROOT / "library" / "parts"
SCHEMA = ROOT / "templates" / "part.schema.json"
BOARDS = (COMPONENTS_DIR / "app" / "runit" / "board.json", COMPONENTS_DIR / "app" / "runit" / "board_devkit.json")


def devices():
    out = {}
    for path in (GENERATED / "devices").glob("device_*.json"):
        doc = json.loads(read(path))
        out[doc["id"]] = doc
    return out


def unique(problems, where, ids, what):
    seen = set()
    for i in ids:
        if i in seen:
            problems.append(f"{where}: {what} '{i}' twice")
        seen.add(i)


def check(path, doc, types, boards, problems):
    where = rel(path)
    if doc["id"] != path.parent.name:
        problems.append(f"{where}: id '{doc['id']}' is not its folder's name '{path.parent.name}'")
    drawing = path.parent / doc["graphic"]
    if not drawing.exists():
        problems.append(f"{where}: its drawing {doc['graphic']} is missing")
    else:
        box = re.search(r'viewBox="([\d.\s-]+)"', read(drawing))
        size = doc["size_mm"]
        if not box or [float(v) for v in box.group(1).split()][2:] != [float(size["w"]), float(size["h"])]:
            problems.append(f"{where}: {doc['graphic']} has no viewBox '0 0 {size['w']} {size['h']}' (its size in mm)")
    device = doc.get("device")
    fields = {}
    if device:
        descriptor = types.get(device["type"])
        if descriptor is None:
            problems.append(f"{where}: device type '{device['type']}' is not a published device descriptor")
        else:
            fields = {f["id"]: f for f in descriptor.get("config", {}).get("fields", [])}
            presets = {p["id"] for p in descriptor.get("presets", [])}
            if "preset" in device and device["preset"] not in presets:
                problems.append(f"{where}: preset '{device['preset']}' is not one of {device['type']}'s ({', '.join(sorted(presets)) or 'none'})")
            for key in device.get("config", {}):
                if key not in fields:
                    problems.append(f"{where}: config '{key}' is not a field of {device['type']}")
    if "board" in doc and (doc["board"]["name"], doc["board"]["revision"]) not in boards:
        problems.append(f"{where}: no board.json is board '{doc['board']['name']}' revision {doc['board']['revision']}")
    nets = {n["id"]: n for n in doc.get("nets", [])}
    unique(problems, where, [n["id"] for n in doc.get("nets", [])], "net")
    unique(problems, where, [c["id"] for c in doc["connectors"]], "connector")
    for connector in doc["connectors"]:
        unique(problems, where, [p["id"] for p in connector["pins"]], f"pin of {connector['id']}")
        if connector["type"] == "pad" and any("at" not in p for p in connector["pins"]):
            problems.append(f"{where}: {connector['id']}: every pin of a pad connector needs 'at'")
        if connector["type"] == "wire" and "from" not in connector:
            problems.append(f"{where}: {connector['id']}: a wire needs 'from'")
        for pin in connector["pins"]:
            at = f"{where}: {connector['id']}.{pin['id']}"
            roles = [k for k in ("gnd", "net", "device_pin", "config", "bus", "hbridge", "motor", "nc") if k in pin]
            if len(roles) > 1:
                problems.append(f"{at}: one role only ({', '.join(roles)})")
            if "net" in pin and pin["net"] not in nets:
                problems.append(f"{at}: net '{pin['net']}' is not a net of the part")
            if "hbridge" in pin and pin["hbridge"].get("supply") not in (None, *nets):
                problems.append(f"{at}: supply '{pin['hbridge']['supply']}' is not a net of the part")
            if "config" in pin:
                field = fields.get(pin["config"])
                if not field or field.get("type") != "pin":
                    problems.append(f"{at}: config '{pin['config']}' is not a pin field of the part's device")
            if pin.get("device_pin", {}).get("device") == "self" and not device:
                problems.append(f"{at}: device_pin 'self' but the part has no device")
            if "motor" in pin and not any(n["role"] == "sink" for n in nets.values()):
                problems.append(f"{at}: a motor lead needs a sink net (what the motor draws)")
    elements = doc.get("power", {}).get("elements", [])
    unique(problems, where, [e["id"] for e in elements], "power element")
    for element in elements:
        ins = element.get("in", [])
        for net in ([ins] if isinstance(ins, str) else ins) + ([element["out"]] if "out" in element else []):
            if net not in nets:
                problems.append(f"{where}: element {element['id']}: net '{net}' is not a net of the part")
        if element["kind"] in ("fuse", "regulator", "mux") and ("in" not in element or "out" not in element):
            problems.append(f"{where}: element {element['id']}: a {element['kind']} needs 'in' and 'out'")
        if element["kind"] == "mux" and len(element.get("inputs", [])) not in (0, len(ins)):
            problems.append(f"{where}: element {element['id']}: one name per input")


def main():
    validator = Draft202012Validator(json.loads(read(SCHEMA)))
    types = devices()
    boards = set()
    for path in BOARDS:
        if path.exists():
            doc = json.loads(read(path))
            boards.add((doc["name"], doc["revision"]))
    problems, checked = [], 0
    for path in sorted(LIBRARY.glob("*/part.json")):
        checked += 1
        doc = json.loads(read(path))
        errors = sorted(validator.iter_errors(doc), key=lambda e: list(map(str, e.path)))
        for error in errors:
            where = "/".join(map(str, error.path)) or "(root)"
            problems.append(f"{rel(path)}: {where}: {error.message[:140]}")
        if not errors:
            check(path, doc, types, boards, problems)
    if problems:
        fail(f"{len(problems)} problem(s) in {checked} library parts:\n  " + "\n  ".join(problems))
    print(f"{checked} library parts conform to part.schema.json; their devices, presets, fields and nets exist")


if __name__ == "__main__":
    main()
