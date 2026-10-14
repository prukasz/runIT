"""Generate the board catalog: the onboard devices, their fixed device IDs and wiring.

Source of truth:
- components/runit/runit_board_defs.h: `#define DEVICE_ID_<NAME> <id>  //@STATIC_DEVICE`,
  the I2C buses (`SYS_I2C_BUS_INTERNAL` / `SYS_I2C_BUS_USER`) and named board
  pins (`#define RUNIT_BOARD_<...> <pin>  // <label>`)
- components/runit/runit_board_cfg.c: `SYS_DEVICE_CREATE_ONBOARD(&g_<driver>_class, &(d_<driver>_cfg_t){.device_id = DEVICE_ID_<NAME>, ...})`,
  which names the driver; when data-structures/devices/device_<driver>.generated.json
  exists, the device links to that descriptor and takes its title. From the
  create call's config: `.i2c_bus`, `.i2c_addr` and every
  `SYS_IO_PIN_INIT(device, pin, mode)` (the pins the device takes). The
  `#if RUNIT_BOARD_DEV_<X>` bring-up switch around it says whether it is
  installed. `sys_io_set_mode` / `sys_io_set_level` calls in
  runit_board_devices_init are the board's own pin setup (status LEDs …).

The app names a device ID (in error payloads, events, pin references) from
this file, shows which pins onboard devices own, and puts user devices on the
user I2C bus.

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
CREATE_RE = re.compile(r"SYS_DEVICE_CREATE_ONBOARD\(\s*&g_(?P<driver>\w+?)_class\s*,\s*&\(d_\w+?_cfg_t\)\s*\{\s*\.device_id\s*=\s*(?P<symbol>DEVICE_ID_\w+)")
SWITCH_RE = re.compile(r"^\s*#define\s+(?P<name>RUNIT_BOARD_DEV_\w+)\s+(?P<value>\d+)", re.MULTILINE)
BUS_RE = re.compile(r"^\s*#define\s+(?P<name>SYS_I2C_BUS_(?P<kind>INTERNAL|USER))\s+(?P<value>\d+)", re.MULTILINE)
BOARD_PIN_RE = re.compile(r"^\s*#define\s+(?P<name>RUNIT_BOARD_\w+)\s+(?P<value>\d+)\s*//\s*(?P<label>.*?)\s*$", re.MULTILINE)
FIELD_RE = re.compile(r"\.(?P<field>\w+)\s*=\s*(?P<value>\{[^{}]*\}|SYS_IO_PIN_INIT\([^()]*\)|SYS_IO_PIN_NONE_INIT|[\w]+)")
PIN_INIT_RE = re.compile(r"SYS_IO_PIN_INIT\(\s*(?P<device>\w+)\s*,\s*(?P<pin>\w+)\s*,\s*(?P<mode>\w+)\s*\)|SYS_IO_PIN_NONE_INIT")
LOOP_RE = re.compile(r"for\s*\(\s*uint8_t\s+(?P<var>\w+)\s*=\s*(?P<first>\w+)\s*;\s*(?P=var)\s*<=\s*(?P<last>\w+)\s*;\s*(?P=var)\+\+\s*\)\s*\{(?P<body>.*?)\n\s*\}", re.DOTALL)
PIN_CALL_RE = re.compile(r"sys_io_set_(?P<call>mode|level)\(\s*SYS_IO_PIN\(\s*(?P<device>\w+)\s*,\s*(?P<pin>\w+)\s*,\s*(?P<mode>\w+)\s*\)\s*(?:,\s*(?P<level>true|false)\s*)?\)")
ESP_PIN_RE = re.compile(r"^\s*#define\s+SYS_PIN_(?P<name>\w+)\s+(?P<pin>\d+)\s*(?://\s*(?P<note>.*?))?\s*$", re.MULTILINE)
COMMENT_RE = re.compile(r"/\*\s*(?P<text>.*?)\s*\*/", re.DOTALL)


def fail(message: str) -> None:
    raise SystemExit(f"ERROR: {message}")


def balanced(text: str, open_at: int) -> int:
    """Index just past the parenthesis that closes the one at open_at."""
    depth = 0
    for index in range(open_at, len(text)):
        if text[index] == "(":
            depth += 1
        elif text[index] == ")":
            depth -= 1
            if depth == 0:
                return index + 1
    fail(f"{CFG_PATH.name}: unbalanced parentheses after offset {open_at}")


def switch_at(cfg: str, position: int, switches: dict) -> bool:
    """Whether the `#if RUNIT_BOARD_DEV_<X>` block around position is on (outside any: yes)."""
    stack = []
    for match in re.finditer(r"^\s*#(?P<kind>if|ifdef|ifndef|endif)\b(?P<rest>.*)$", cfg[:position], re.MULTILINE):
        if match.group("kind") == "endif":
            if stack:
                stack.pop()
        else:
            stack.append(match.group("rest").strip())
    for condition in stack:
        name = condition.split()[0] if condition else ""
        if name in switches and not switches[name]:
            return False
    return True


def board_wiring(defs: str, cfg: str, device_ids: dict) -> tuple[dict, dict, list]:
    """Per device (symbol): installed, i2c, pins; plus the buses and the board's own pin setup."""
    switches = {match.group("name"): int(match.group("value")) != 0 for match in SWITCH_RE.finditer(cfg)}
    buses = {match.group("name"): int(match.group("value")) for match in BUS_RE.finditer(defs)}
    named_pins = {match.group("name"): (int(match.group("value")), match.group("label")) for match in BOARD_PIN_RE.finditer(defs)}

    def number(token: str, where: str) -> int:
        if re.fullmatch(r"0x[0-9a-fA-F]+|\d+", token):
            return int(token, 0)
        if token in named_pins:
            return named_pins[token][0]
        if token in buses:
            return buses[token]
        fail(f"{CFG_PATH.name}: {where}: can't resolve '{token}'")

    def device_id(token: str, where: str) -> int:
        if token not in device_ids:
            fail(f"{CFG_PATH.name}: {where}: '{token}' is not a //@STATIC_DEVICE")
        return device_ids[token]

    wiring = {}
    create_spans = []
    for match in CREATE_RE.finditer(cfg):
        start = cfg.index("(", match.start())
        end = balanced(cfg, start)
        create_spans.append((match.start(), end))
        body = cfg[match.end():end]
        symbol = match.group("symbol")
        entry = {"installed": switch_at(cfg, match.start(), switches)}
        pins = []
        for field in FIELD_RE.finditer(body):
            name, value = field.group("field"), field.group("value")
            if name == "i2c_bus":
                entry.setdefault("i2c", {})["bus"] = number(value, f"{symbol}.i2c_bus")
            elif name == "i2c_addr":
                entry.setdefault("i2c", {})["address"] = number(value, f"{symbol}.i2c_addr")
            elif "SYS_IO_PIN" in value:
                refs = list(PIN_INIT_RE.finditer(value))
                for index, ref in enumerate(refs):
                    if not ref.group("device"):
                        continue  # SYS_IO_PIN_NONE_INIT: not connected
                    pin = {"use": name if not value.startswith("{") else f"{name}[{index}]", "device": device_id(ref.group("device"), f"{symbol}.{name}"),
                           "pin": number(ref.group("pin"), f"{symbol}.{name}"), "mode": ref.group("mode")}
                    pins.append(pin)
        if pins:
            entry["pins"] = pins
        wiring[symbol] = entry

    # The board's own pin setup: sys_io_set_mode / sys_io_set_level outside the create calls.
    init = re.search(r"err_h\s+runit_board_devices_init\s*\(\s*void\s*\)\s*\{", cfg)
    setup = {}
    if init:
        body_start = init.end() - 1
        body_end = balanced(cfg.replace("{", "(").replace("}", ")"), body_start)
        calls = []
        for loop in LOOP_RE.finditer(cfg, body_start, body_end):
            first, last = number(loop.group("first"), "loop"), number(loop.group("last"), "loop")
            for value in range(first, last + 1):
                for call in PIN_CALL_RE.finditer(loop.group("body")):
                    calls.append((loop.start(), call, value if call.group("pin") == loop.group("var") else None))
        loop_spans = [(loop.start(), loop.end()) for loop in LOOP_RE.finditer(cfg, body_start, body_end)]
        for call in PIN_CALL_RE.finditer(cfg, body_start, body_end):
            if any(start <= call.start() < end for start, end in create_spans + loop_spans):
                continue
            calls.append((call.start(), call, None))
        for position, call, loop_pin in sorted(calls, key=lambda entry: entry[0]):
            device = device_id(call.group("device"), "pin setup")
            pin = loop_pin if loop_pin is not None else number(call.group("pin"), "pin setup")
            key = (device, pin)
            entry = setup.setdefault(key, {"device": device, "pin": pin, "mode": call.group("mode"), "installed": switch_at(cfg, position, switches)})
            if call.group("call") == "level":
                entry["level"] = call.group("level") == "true"
            # Label: the board pin define's comment, else the comment right above the call.
            label = next((text for name, (value, text) in named_pins.items() if value == pin and name.startswith("RUNIT_BOARD_TCA_") and device == device_ids.get("DEVICE_ID_TCA6424A")), None)
            if label is None:
                comments = [match for match in COMMENT_RE.finditer(cfg, body_start, position)]
                # A comment labels the calls under it, up to the next blank line.
                if comments and not re.search(r"\n\s*\n", cfg[comments[-1].end():position]):
                    label = " ".join(comments[-1].group("text").split()).rstrip(".")
            if label:
                entry.setdefault("label", label)
    i2c = {kind.lower(): value for kind, value in ((name.removeprefix("SYS_I2C_BUS_"), value) for name, value in buses.items())}
    # ESP pins the board uses outside sys_io (SYS_PIN_* in runit_board_defs.h, e.g. the I2C buses).
    gpio = device_ids.get("DEVICE_ID_GPIO_ESP")
    reserved = []
    for match in ESP_PIN_RE.finditer(defs):
        label = match.group("name").replace("_", " ")
        reserved.append({"device": gpio, "pin": int(match.group("pin")), "label": f"{label} ({match.group('note')})" if match.group("note") else label})
    return wiring, i2c, list(setup.values()), reserved


def build_document() -> dict:
    defs = DEFS_PATH.read_text(encoding="utf-8", errors="replace")
    cfg = CFG_PATH.read_text(encoding="utf-8", errors="replace")
    drivers = {match.group("symbol"): match.group("driver") for match in CREATE_RE.finditer(cfg)}
    device_ids = {match.group("symbol"): int(match.group("id")) for match in STATIC_DEVICE_RE.finditer(defs)}
    wiring, i2c, pin_setup, reserved = board_wiring(defs, cfg, device_ids)
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
        entry.update(wiring.get(symbol, {"installed": False}))
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
        "i2c_buses": i2c,
        "devices": sorted(devices, key=lambda device: device["id"]),
        "pin_setup": pin_setup,
        "reserved_pins": reserved,
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
