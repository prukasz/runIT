"""Generic IC symbol of a device: the pins CSV -> ports + an SVG with a clickable terminal per pin.

The CSV is `components/devices/device_<chip>/app/device_<chip>.pins.csv`, one row per package pin,
copied from the datasheet's pin table. The symbol shape (dual-row or QFN, see device_symbol_svg.py) follows
`symbol.package`; every chip of a shape looks alike and only the pins differ.

Each pin is `<g id="pin-NAME" class="port ..." data-port="NAME">` with a `<circle id="pin-NAME-t">`
terminal at the outer end of its stub: the app attaches wires to the terminal and treats the whole
group as the click target. Colours are CSS variables with defaults, so the app can theme the symbol.
"""
import csv
import html
import re
from pathlib import Path
from typing import Dict, List

COLUMNS = ["pin", "name", "kind", "dir", "modes", "default_mode", "group", "mV_min", "mV_max", "bind_pin", "cfg_field", "note"]
KINDS = {"supply_in", "supply_out", "gnd", "io", "bus"}
DIRS = {"in", "out", "inout"}
MODES = {"BINARY", "PWM", "ADC", "DAC"}
NAME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_]*$")



def pins_path(header: Path) -> Path:
    return header.parent.parent / "app" / f"{header.stem}.pins.csv"


def number(value: str, column: str, where: str) -> int:
    try:
        return int(value, 0)
    except ValueError:
        raise SystemExit(f"ERROR: {where}: column {column} is not a number: '{value}'")


def read_ports(path: Path) -> List[dict]:
    """The ports of a pins CSV, in pin order, checked: contiguous pin numbers, unique names, known kinds / directions / modes."""
    with path.open(encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(line for line in handle if line.strip() and not line.startswith("#"))
        if reader.fieldnames != COLUMNS:
            raise SystemExit(f"ERROR: {path.name}: columns are {reader.fieldnames}, want {COLUMNS}")
        rows = list(reader)
    ports: List[dict] = []
    for row in rows:
        where = f"{path.name}: pin {row['pin']}"
        port: dict = {"name": row["name"], "pin": number(row["pin"], "pin", where), "kind": row["kind"], "dir": row["dir"]}
        if not NAME_RE.match(port["name"]):
            raise SystemExit(f"ERROR: {where}: name '{port['name']}' must be letters, digits and _ (it becomes an SVG id)")
        if port["kind"] not in KINDS or port["dir"] not in DIRS:
            raise SystemExit(f"ERROR: {where}: kind '{port['kind']}' / dir '{port['dir']}' not in {sorted(KINDS)} / {sorted(DIRS)}")
        modes = [m for m in row["modes"].split("|") if m]
        unknown = sorted(set(modes) - MODES)
        if unknown:
            raise SystemExit(f"ERROR: {where}: unknown mode(s) {unknown}; allowed: {sorted(MODES)}")
        if port["kind"] == "io" and not modes:
            raise SystemExit(f"ERROR: {where}: an io port needs modes")
        if modes:
            port["modes"] = modes
            port["default_mode"] = row["default_mode"] or modes[0]
            if port["default_mode"] not in modes:
                raise SystemExit(f"ERROR: {where}: default_mode '{port['default_mode']}' is not one of the modes")
        for column in ("group", "cfg_field", "note"):
            if row[column]:
                port[column] = row[column]
        for column in ("mV_min", "mV_max", "bind_pin"):
            if row[column]:
                port[column] = number(row[column], column, where)
        port["svg"] = f"pin-{port['name']}"
        ports.append(port)
    ports.sort(key=lambda p: p["pin"])
    if [p["pin"] for p in ports] != list(range(1, len(ports) + 1)):
        raise SystemExit(f"ERROR: {path.name}: pin numbers must run 1..{len(ports)} without gaps")
    names = [p["name"] for p in ports]
    if len(set(names)) != len(names):
        raise SystemExit(f"ERROR: {path.name}: duplicate port names {sorted({n for n in names if names.count(n) > 1})}")
    return ports


from device_symbol_svg import render_svg  # noqa: E402,F401  (the SVG half, split out; callers use device_symbol.render_svg)
