"""The SVG of a device symbol (ports from device_symbol.read_ports), in one of two package shapes picked by `symbol.package`:

  - dual-row (TSSOP, SSOP, MSOP, SOIC, SOP, SO, DIP): pins 1..N/2 down the left side, the rest up the
    right side (pin 1 top left, as in the datasheet figures);
  - QFN / DFN: a square, pins counter-clockwise from pin 1 at the top of the left side: the left side
    down, the bottom left to right, the right side up, the top right to left.

Every chip of a shape looks alike and only the pins differ. The symbol is compact (pin pitch 14 px);
the app shows it at a scale it chooses.
"""
import html
import re
from typing import Dict, List

PAD, STUB, ROW, TOP, HEAD = 8, 14, 14, 34, 6  # px
DUAL_W = 112  # body width of the dual-row symbol
QFN_INSET = 28  # QFN pins keep this far from the corners: the names of two sides meet there
TERMINAL = 3  # terminal radius
DUAL = ("TSSOP", "SSOP", "MSOP", "SOIC", "SOP", "SO", "DIP")
QFN = ("QFN", "DFN")

STYLE = """
.body { fill: var(--sym-body, #f4f6f8); stroke: var(--sym-line, #9aa5b1); }
.label { font: 600 11px system-ui, sans-serif; fill: var(--sym-text, #1f2933); }
.package { font: 8px system-ui, sans-serif; fill: var(--sym-muted, #52606d); }
.name { font: 8px system-ui, sans-serif; fill: var(--sym-text, #1f2933); }
.num { font: 6px system-ui, sans-serif; fill: var(--sym-muted, #52606d); }
.stub { stroke: var(--sym-line, #9aa5b1); stroke-width: 1; }
.terminal { fill: var(--sym-body, #fff); stroke: var(--sym-line, #9aa5b1); stroke-width: 1; }
.hit { fill: transparent; cursor: pointer; }
.kind-supply_in .terminal, .kind-supply_out .terminal { stroke: #e5933a; }
.kind-gnd .terminal { stroke: #52606d; }
.kind-bus .terminal { stroke: #16a34a; }
.kind-io.dir-out .terminal { stroke: #3b82f6; }
.kind-io.dir-in .terminal { stroke: #8b5cf6; }
.port:hover .terminal, .port:focus .terminal, .port.selected .terminal { fill: var(--sym-hot, #3b82f6); }
.port:focus { outline: none; }
.is-hidden .name { fill: var(--sym-muted, #52606d); opacity: 0.45; }
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) { --sym-body: #1f2933; --sym-line: #7b8794; --sym-text: #e4e7eb; --sym-muted: #9aa5b1; }
}
:root[data-theme="dark"] { --sym-body: #1f2933; --sym-line: #7b8794; --sym-text: #e4e7eb; --sym-muted: #9aa5b1; }
"""


def shape_of(package: str) -> str:
    """'dual' or 'qfn' for a package name (TSSOP28, QFN-32 ...); anything else is an error, so a typo cannot pass as a shape. No package: a dual-row symbol (the chip's ports, not its package, are what matters)."""
    if not package:
        return "dual"
    name = re.sub(r"[^A-Za-z].*$", "", package.strip().upper())
    if name in QFN:
        return "qfn"
    if name in DUAL:
        return "dual"
    raise SystemExit(f"ERROR: symbol package '{package}' is not one of {list(DUAL) + list(QFN)} (followed by the pin count)")


# A pin on a symbol: its side, where its stub starts on the body edge and where its terminal is.
Pin = Dict[str, object]


def dual_layout(count: int):
    left_count = (count + 1) // 2
    body_h = left_count * ROW + 2 * HEAD
    body_x, body_y = PAD + STUB, TOP
    width, height = 2 * (PAD + STUB) + DUAL_W, TOP + body_h + 10
    pins: List[Pin] = []
    for index in range(count):
        left = index < left_count
        row = index if left else count - 1 - index  # the right side runs bottom to top
        cy = body_y + HEAD + ROW * row + ROW // 2
        pins.append({"side": "left" if left else "right", "edge": (body_x if left else body_x + DUAL_W, cy), "end": (PAD if left else width - PAD, cy)})
    return width, height, (body_x, body_y, DUAL_W, body_h), pins


def qfn_layout(count: int):
    base, extra = divmod(count, 4)
    sizes = [base + (1 if side < extra else 0) for side in range(4)]  # left, bottom, right, top
    side = max(sizes) * ROW + 2 * QFN_INSET
    body_x, body_y = PAD + STUB, TOP + STUB
    width, height = 2 * (PAD + STUB) + side, TOP + 2 * STUB + side + 10
    pins: List[Pin] = []
    for which, size in enumerate(sizes):
        pitch = (side - 2 * QFN_INSET) / size
        for slot in range(size):
            at = QFN_INSET + pitch * (slot + 0.5)
            if which == 0:    # left, down
                pins.append({"side": "left", "edge": (body_x, body_y + at), "end": (PAD, body_y + at)})
            elif which == 1:  # bottom, left to right
                pins.append({"side": "bottom", "edge": (body_x + at, body_y + side), "end": (body_x + at, body_y + side + STUB)})
            elif which == 2:  # right, up
                pins.append({"side": "right", "edge": (body_x + side, body_y + side - at), "end": (width - PAD, body_y + side - at)})
            else:             # top, right to left
                pins.append({"side": "top", "edge": (body_x + side - at, body_y), "end": (body_x + side - at, body_y - STUB)})
    return width, height, (body_x, body_y, side, side), pins


def num(value: float) -> str:
    return f"{value:.1f}".rstrip("0").rstrip(".")


def render_svg(label: str, package: str, ports: List[dict], hidden=()) -> str:
    """`hidden`: names of pins this variant does not bring out (a breakout board without them): their slot stays, greyed inside the body, with no stub, terminal or hit area."""
    count = len(ports)
    shape = shape_of(package)
    width, height, (bx, by, bw, bh), pins = (dual_layout if shape == "dual" else qfn_layout)(count)
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" width="{width}" height="{height}">',
           f"<title>{html.escape(label)}</title>",
           f"<style>{STYLE}</style>",
           f'<text class="label" x="{width // 2}" y="13" text-anchor="middle">{html.escape(label)}</text>',
           f'<text class="package" x="{width // 2}" y="24" text-anchor="middle">{html.escape(package + ' · ' if package else '')}{count} ports</text>',
           f'<rect class="body" x="{bx}" y="{by}" width="{bw}" height="{bh}" rx="3"/>']
    if shape == "dual":
        out.append(f'<path class="body" d="M{num(bx + bw / 2 - 6)} {by} a6 6 0 0 0 12 0" fill="none"/>')
    out.append(f'<circle class="terminal" cx="{bx + 7}" cy="{by + 7}" r="2"/>')
    for port, pin in zip(ports, pins):
        (x1, y1), (x2, y2), side = pin["edge"], pin["end"], pin["side"]
        mx, my = (x1 + x2) / 2, (y1 + y2) / 2
        title = f"{port['name']} · pin {port['pin']}" + (f" · {port['note']}" if port.get("note") else "")
        classes = f"port kind-{port['kind']} dir-{port['dir']}" + (f" group-{port['group']}" if port.get("group") else "")
        attributes = f'data-port="{html.escape(port["name"])}" data-pin="{port["pin"]}" data-kind="{port["kind"]}" data-dir="{port["dir"]}"'
        if port.get("modes"):
            attributes += f' data-modes="{"|".join(port["modes"])}"'
        name = html.escape(port["name"])
        if port["name"] in hidden:
            ghost = f'<text class="name" x="{num(x1 + 5)}" y="{num(y1 + 3)}" text-anchor="start">{name}</text>' if side == "left" else f'<text class="name" x="{num(x1 - 5)}" y="{num(y1 + 3)}" text-anchor="end">{name}</text>' if side == "right" else f'<text class="name" transform="translate({num(x1 + 3)} {num(y1 + (5 if side == "top" else -5))}) rotate({90 if side == "top" else -90})" text-anchor="start">{name}</text>'
            out.append(f'<g id="{port["svg"]}" class="port is-hidden" data-port="{html.escape(port["name"])}" data-pin="{port["pin"]}" data-hidden="true">{ghost}</g>')
            continue
        horizontal = side in ("left", "right")
        if horizontal:
            hit = f'x="{num(min(x1, x2))}" y="{num(y1 - ROW / 2)}" width="{num(abs(x2 - x1))}" height="{ROW}"'
            number = f'<text class="num" x="{num(mx)}" y="{num(my - 2)}" text-anchor="middle">{port["pin"]}</text>'
        else:
            hit = f'x="{num(x1 - ROW / 2)}" y="{num(min(y1, y2))}" width="{ROW}" height="{num(abs(y2 - y1))}"'
            number = f'<text class="num" x="{num(mx + 2)}" y="{num(my + 2)}" text-anchor="start">{port["pin"]}</text>'
        if side == "left":
            label_text = f'<text class="name" x="{num(x1 + 5)}" y="{num(y1 + 3)}" text-anchor="start">{name}</text>'
        elif side == "right":
            label_text = f'<text class="name" x="{num(x1 - 5)}" y="{num(y1 + 3)}" text-anchor="end">{name}</text>'
        elif side == "top":     # reads downwards from the edge
            label_text = f'<text class="name" transform="translate({num(x1 + 3)} {num(y1 + 5)}) rotate(90)" text-anchor="start">{name}</text>'
        else:                   # bottom: reads upwards from the edge
            label_text = f'<text class="name" transform="translate({num(x1 + 3)} {num(y1 - 5)}) rotate(-90)" text-anchor="start">{name}</text>'
        out.append(f'<g id="{port["svg"]}" class="{classes}" {attributes} tabindex="0" role="button" aria-label="{name}">'
                   f"<title>{html.escape(title)}</title>"
                   f'<rect class="hit" {hit}/>'
                   f'<line class="stub" x1="{num(x1)}" y1="{num(y1)}" x2="{num(x2)}" y2="{num(y2)}"/>'
                   f'<circle id="{port["svg"]}-t" class="terminal" cx="{num(x2)}" cy="{num(y2)}" r="{TERMINAL}"/>'
                   f"{number}{label_text}</g>")
    out.append("</svg>")
    return "\n".join(out) + "\n"
