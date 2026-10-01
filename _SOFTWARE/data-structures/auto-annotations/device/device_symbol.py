"""Generic IC symbol of a device: the pins CSV -> ports + an SVG with a clickable terminal per pin.

The CSV is `components/devices/device_<chip>/device_<chip>.pins.csv`, one row per package pin,
copied from the datasheet's pin table. The symbol is a dual-row package: pins 1..N/2 down the
left side, the rest up the right side (pin 1 top left, as in the datasheet figures), so every
chip looks alike and only the pins differ.

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

PAD, STUB, BODY_W, ROW, TOP, HEAD = 16, 32, 220, 24, 56, 8  # px


def pins_path(header: Path) -> Path:
    return header.parent.parent / f"{header.stem}.pins.csv"


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


STYLE = """
.body { fill: var(--sym-body, #f4f6f8); stroke: var(--sym-line, #9aa5b1); }
.label { font: 600 13px system-ui, sans-serif; fill: var(--sym-text, #1f2933); }
.package { font: 11px system-ui, sans-serif; fill: var(--sym-muted, #52606d); }
.name { font: 11px system-ui, sans-serif; fill: var(--sym-text, #1f2933); }
.num { font: 9px system-ui, sans-serif; fill: var(--sym-muted, #52606d); }
.stub { stroke: var(--sym-line, #9aa5b1); stroke-width: 1.5; }
.terminal { fill: var(--sym-body, #fff); stroke: var(--sym-line, #9aa5b1); stroke-width: 1.5; }
.hit { fill: transparent; cursor: pointer; }
.kind-supply_in .terminal, .kind-supply_out .terminal { stroke: #e5933a; }
.kind-gnd .terminal { stroke: #52606d; }
.kind-bus .terminal { stroke: #16a34a; }
.kind-io.dir-out .terminal { stroke: #3b82f6; }
.kind-io.dir-in .terminal { stroke: #8b5cf6; }
.port:hover .terminal, .port:focus .terminal, .port.selected .terminal { fill: var(--sym-hot, #3b82f6); }
.port:focus { outline: none; }
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) { --sym-body: #1f2933; --sym-line: #7b8794; --sym-text: #e4e7eb; --sym-muted: #9aa5b1; }
}
:root[data-theme="dark"] { --sym-body: #1f2933; --sym-line: #7b8794; --sym-text: #e4e7eb; --sym-muted: #9aa5b1; }
"""


def render_svg(label: str, package: str, ports: List[dict]) -> str:
    count = len(ports)
    left_count = (count + 1) // 2
    body_x, body_h = PAD + STUB, left_count * ROW + 2 * HEAD
    width, height = 2 * (PAD + STUB) + BODY_W, TOP + body_h + 16
    right_x = body_x + BODY_W
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" width="{width}" height="{height}">',
           f"<title>{html.escape(label)} {html.escape(package)}</title>",
           f"<style>{STYLE}</style>",
           f'<text class="label" x="{width // 2}" y="22" text-anchor="middle">{html.escape(label)}</text>',
           f'<text class="package" x="{width // 2}" y="38" text-anchor="middle">{html.escape(package)} · {count} pins</text>',
           f'<rect class="body" x="{body_x}" y="{TOP}" width="{BODY_W}" height="{body_h}" rx="4"/>',
           f'<path class="body" d="M{width // 2 - 10} {TOP} a10 10 0 0 0 20 0" fill="none"/>',
           f'<circle class="terminal" cx="{body_x + 10}" cy="{TOP + 10}" r="2.5"/>']
    for index, port in enumerate(ports):
        left = index < left_count
        row = index if left else count - 1 - index  # the right side runs bottom to top
        cy = TOP + HEAD + ROW * row + ROW // 2
        edge, end = (body_x, PAD) if left else (right_x, width - PAD)
        anchor_name, name_x = ("start", body_x + 10) if left else ("end", right_x - 10)
        hit_x = PAD - 4 if left else right_x
        mid = (edge + end) / 2
        title = f"{port['name']} · pin {port['pin']}" + (f" · {port['note']}" if port.get("note") else "")
        classes = f"port kind-{port['kind']} dir-{port['dir']}" + (f" group-{port['group']}" if port.get("group") else "")
        attributes = f'data-port="{html.escape(port["name"])}" data-pin="{port["pin"]}" data-kind="{port["kind"]}" data-dir="{port["dir"]}"'
        if port.get("modes"):
            attributes += f' data-modes="{"|".join(port["modes"])}"'
        out.append(f'<g id="{port["svg"]}" class="{classes}" {attributes} tabindex="0" role="button" aria-label="{html.escape(port["name"])}">'
                   f"<title>{html.escape(title)}</title>"
                   f'<rect class="hit" x="{hit_x}" y="{cy - ROW // 2}" width="{STUB + 4}" height="{ROW}"/>'
                   f'<line class="stub" x1="{edge}" y1="{cy}" x2="{end}" y2="{cy}"/>'
                   f'<circle id="{port["svg"]}-t" class="terminal" cx="{end}" cy="{cy}" r="4.5"/>'
                   f'<text class="num" x="{mid}" y="{cy - 4}" text-anchor="middle">{port["pin"]}</text>'
                   f'<text class="name" x="{name_x}" y="{cy + 4}" text-anchor="{anchor_name}">{html.escape(port["name"])}</text>'
                   "</g>")
    out.append("</svg>")
    return "\n".join(out) + "\n"
