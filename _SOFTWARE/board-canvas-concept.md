# Board canvas — concept draft

Status: draft, not yet in `features.md`. Source: the owner's raw notes (kept at the end).

## 1. Idea

A **hardware canvas** inside the Devices view (the canvas already prepared there). The runIT board sits fixed in the centre, drawn as a PCB with its real connectors (USB, XT60, screw terminals, gold-pin headers). Devices are drawn around it as SVG parts and are wired to the board with wires that behave like real electrical connections (a servo has three wires: GND, V+, signal).

Everything is generated from data (`.json`, `.csv`, optional `.svg`). Nothing is hand-placed per device in app code.

## 2. What it is for

1. **Easier configuration.** Show which pins and power sources are available and suggested for the thing being connected; show the IO and power networks with their values and limits.
2. **Visual device creation and hook-up.** Create a device by dropping it and wiring it; wiring pre-fills install fields and pin settings (pin ref, mode, I2C address from the A0..A5 ties, …).
3. **Quick electrical overview** of the project's devices.

## 3. Layout

- **Board fixed in the centre**, not movable. Source art: [board-simple.svg](board-simple.svg) (616×1000, groups already named per terminal: `ain0..7`, `i2c-sda`, `uart-tx`, `spi-mosi`, `gpio-io46`, `pwm1-gnd/-reg2/-pwm`, `t3v3-3v3`, `esp32`, `btn-en`, …). The group id is the natural key to a board pin.
- **Connectors are palette items**, placed on the board edge as on the real PCB (screw terminal, gold pin, XT60, USB). The palette lists the connector types.
- **Devices** come from the device palette as today and are placed freely around the board.
- **Always-on grid.** Placement of devices and connectors on the canvas snaps to the grid. Artwork is **not** required to be grid-sized: on import an image is scaled to a whole number of grid units (aspect kept), and pin anchors are stored in the image's own normalised coordinates (see §4).

## 4. Data and generation

Per device, next to the existing files in `components/devices/device_<x>/`:

| File | Gives | Status |
|---|---|---|
| `app/device_<x>.json` | title, tags, `symbol` (`label`, `package`) | exists |
| `app/device_<x>.pins.csv` | pins: `name`, `kind` (io / bus / gnd / supply_in), `dir`, `modes`, `group`, `mV_min`, `mV_max`, `bind_pin`, `note` | exists |
| `images/<x>.svg` or `images/<x>.png` | background picture: the bare IC, a breakout / dev board, a servo, any component | optional |
| `app/canvas/<variant>/layout.json` (+ the picture beside it) | pin anchor positions over the picture (made in the editor, §4a) | produced by the app |

The picture is **only a background**. It carries no ids and no anchors; it can be a vector drawing or a photo / render (PNG). The pins.csv says *what* pins exist; the layout says *where* each one sits on the picture.

**Fallback template.** With no picture, a symbol is generated from `symbol.package` + pins.csv: an IC body with pins placed by package (TSSOP28 → two rows, pin 1 at top-left), labelled with `name`, coloured by `kind` / `group`. Its anchors are generated too, so no editing is needed. PCA9685 is the reference case.

**Same model for everything.** Device, breakout board, connector and the runIT board itself are all "picture + pins list + anchor layout". `board-simple.svg` is therefore just a background; its per-terminal group ids are no longer the key to a pin (the layout is). Connector palette items (screw terminal, gold pin, XT60, USB, servo header) are small pictures with a pin list and layout in the same format.

**Why this is better than anchors in the art.**
- One IC can have several looks: the bare chip, or different breakout boards / dev boards of it, each with its own picture and layout but the same pins.csv.
- A breakout may expose only some of the IC's pins. The layout lists only the pins that are really reachable, so only those can be wired.
- Any picture works, no need to author or edit SVG ids.

### 4a. Anchor editor ("Edit device")

A standalone helper in the Devices view, opened from a device (or board / connector) template. It works on **one component at a time** and is not part of normal wiring. The normal canvas only places components that already have a saved layout and links them by their anchors.

1. Shows the background picture, scaled on the grid, with pan / zoom.
2. Spawns one **semi-transparent anchor** per pin from the pins.csv in a tray beside the picture (named, coloured by `kind`).
3. The user drags each anchor onto the matching pad / pin / hole in the picture. Optional: snap to a fine sub-grid, nudge with arrow keys, multi-select and align / distribute for pin rows.
4. A pin can be left unplaced = **not exposed** on this picture (it cannot be wired). The editor shows placed / unplaced counts.
5. Rotate / mirror of the whole picture is not edited here; it is a placement property on the canvas (§9.2) and anchors follow it.
6. Per variant, each placed pin can override its electrical limits (`mV_min` / `mV_max`, `dir`, `modes`) over the csv defaults, e.g. a breakout with an onboard regulator accepts 5 V where the bare chip does not. Overrides are stored in the layout file next to the anchor.
7. Save writes the variant files to the component folder through the dev server (§9.13) and keeps a copy in the project snapshot (§9.8, §9.10).

Layout format (positions are fractions 0..1 of the picture's width / height, so replacing the picture with a higher-resolution copy keeps the layout valid):

```json
{
  "image": "images/pca9685_breakout.png",
  "size_cells": [8, 5],
  "anchors": {
    "SDA": { "x": 0.12, "y": 0.40 },
    "SCL": { "x": 0.12, "y": 0.52 },
    "VCC": { "x": 0.05, "y": 0.20, "mV_min": 3000, "mV_max": 5500 },
    "LED0": { "x": 0.80, "y": 0.10 }
  }
}
```

Keys are pins.csv names. Anchors in the layout that are no longer in the csv are dropped on load and flagged; new csv pins show up as unplaced.

**Board.** The runIT board uses the same path: `board-simple.svg` as background, the board descriptor (`components/app/runit/board.json`) for each terminal's pins, voltage and limits, plus a layout over the picture.

## 5. Wires and electrical model

- A wire joins two **pin anchors**. It is a net member, not a decoration: nets are computed from wires (union of connected pins, through connectors).
- **Compatibility** from the csv: `kind` and `dir` (out → in, gnd ↔ gnd, supply ↔ supply), `modes` vs the driver's modes, voltage range (`mV_min` / `mV_max`) against the net's source voltage.
- A net knows its voltage (from the regulator / supply source), its drivers and its loads. Warnings: voltage above a pin's `mV_max`, below `mV_min`, two drivers, floating supply, missing ground.
- Multi-pin parts (servo, I2C sensor) wire as a bundle: drop the connector on a matching board connector and the pins pair up by function; wires can also be drawn pin by pin.
- Wires are stored in the project as references to stable pin IDs, not coordinates (route is only presentation).

## 6. Canvas view options (toggle bar)

| Option | Effect |
|---|---|
| Power networks | highlight supply nets and their flow (source → loads) |
| IO networks | highlight signal nets |
| Device connections | highlight which device is attached to which connector / pin |
| Voltages | show net voltage labels and limit violations |
| Available only | while dragging a wire from a pin, dim every pin that cannot take it (kind, dir, mode, voltage, already used, pin taken by another device) |

## 7. Project and firmware link

- Devices on this canvas are the project's existing `devices` (user devices, I2C bus, pin refs). Placing / wiring edits the same data as the Devices view: device ID, install frame fields (`oe_pin`, `intr_pin` … as `sys_io_pin_ref_t`), pin aliases and default modes. No second source of truth.
- Only the **layout** (position, rotation, connector placement) is new project data: a `hardware` section next to `canvases`, keyed by stable IDs.
- The existing checks (pin clashes, I2C address, PWM frequency warnings, pins only on a lower-ID device) feed the same warnings onto the canvas.

## 8. Suggested phases

1. **Static board view.** Load the runIT template (`board-simple.svg` as background + layout), fixed centre, grid, pan / zoom, hover a terminal → its pin, mode, voltage.
2. **Anchor editor (§4a)** with SVG / PNG backgrounds and the layout file; fallback template generator from `symbol` + pins.csv; palette and placement on the grid (PCA9685 first, as bare chip and as a breakout).
3. **Wires and nets**, compatibility, "available only" highlight, wire stored in the project.
4. **Connector palette** (screw terminal, gold pin, XT60, USB) and placing them on the board edge.
5. **Board creator / editor** (outline, mount holes, fixed parts, connector placement, save as template).
6. **Overlays** (power, IO, connections, voltages) and electrical warnings.
7. **Create-by-wiring**: wiring fills install frames and default settings; custom SVGs (servo) with anchors.

## 9. Decisions (owner, 2026-10-02)

1. **The board is a template, not a constant.** A custom board can be made; "runIT board" (`board-simple.svg` + `board.generated.json`) is just one saved template. The board is fixed in the centre but its connectors are placed on its edge from the palette. A board template = PCB outline, mount holes, fixed parts (MCU, buttons) and the placed connectors with their pin bindings.
2. **Rotation and mirroring** are in v1, for devices and connectors.
3. **Voltage sources:** for now use the settings selected at boot (the regulators are the TPS55289 board devices in `board.generated.json`); net voltages are those boot values. Live / applied voltage changes are out of scope for now.
4. **Wires are visual only**: a connection between two pins, with no length, gauge or other physical property. Nets are still computed from them for checks and highlights.
5. **No new main view.** It lives in the Devices view, which already has the prepared canvas.

Resolved after the first round:

6. **Pin anchors** are positioned by the user in the anchor editor (§4a) over a background picture (SVG or PNG) and stored in a layout file (`app/canvas/<variant>/layout.json`). *(Replaces the earlier idea of anchors carried by connector / pin elements joined to the SVG body.)*
7. **Grid unit** is settled later; art is kept in whole grid units so the unit can change.
8. **Template storage:** both a shared library and the project. A project can use a library template or carry its own copy.
9. **Custom board capabilities:** no extra data source is needed (nothing to decide for now).

10. **Template precedence:** the project carries its own copy of every template it uses (a snapshot inside the project file), so a project opens the same anywhere. The project copy wins over a library template with the same ID and does not track library updates on its own. A library update can be pulled in by hand ("update from library") and a project copy can be saved back to the library.

11. **Board creator / editor** in the same Devices view, for drafting a board quickly: a mode switch between *project* (place devices, wire) and *board edit* (shape the board itself). In board edit the user sets the PCB outline and size on the grid, mount holes, fixed parts (MCU, buttons) and places connectors with their pin bindings (the same palette, rotate / mirror). The result is saved as a template (project and / or library, item 8). The runIT board opens in the same editor as a template.

12. **Variants per device:** one folder per variant (`device_<x>/app/canvas/<variant>/`: `layout.json` + its picture), each with its own picture; the pins csv stays shared in the device root; a variant picker in the palette. (Owner, 2026-10-02.)
13. **The app saves to the repo files itself.** For now the app runs on localhost with the Vite dev server, which exposes a small write endpoint limited to `components/devices/**` (images and layout files; no paths outside, no overwriting of csv / json). Later options, not v1: an embedded-deploy backend, and account-based storage so it also works from the mobile PWA. The project snapshot (§9.10) stays the portable copy in every case.
14. **Per-variant electrical limits** are edited in the anchor editor and stored in the layout (§4a.6).
15. **Editor scope:** the anchor editor is per component only; the normal canvas arranges components and wires between their anchors.

## 9b. Wiring as built (2026-10-02)

Wires, nets, the dependency checks and the view options of §5 and §6 are implemented in the Board canvas.

**Data.** A wire is `{id, a, b}` with an end `{device, pin}`: `device` is `board` or a user device's project ID, `pin` is a board terminal's art ID or a device pin's csv name. Stored in the project as `wires` (references only, never coordinates), next to `boardLayout`, undoable with the devices. Removing a device removes its wires. The board's terminals are described in the board descriptor (`components/app/runit/board.json`): kind, direction, modes, limits, bus signal, the named net (GND, 3V3, REG1, REG2, VS: all terminals of one net are joined on the PCB) and, where known, the sys_io pin behind it.

**Producer / consumer flags.** Every pin has a role, derived from its direction (the csv `dir`, overridable per variant in the anchor editor): `out` = **producer** (drives the signal), `in` = **consumer** (receives it), `inout` = **both** (takes the role the other end needs, like a GPIO). A supply output produces, a supply input consumes, ground is neutral. The role is shown as a flag on the pin lists, in the pin tooltips (`IO3 · both`, `AIN0 · consumer`), in the wire card, and as an arrow along each wire from producer to consumer.

**Connection rules** (`domain/hardware/connect.ts`; the same function drives the refusal of a wire and the "available only" rings): ground to ground; a supply to a supply (two supply outputs only on the same named net); a bus pin to the same bus signal (a UART's TX to RX); otherwise a **producer feeds a consumer**: two producers are refused, a flexible pin fits either end, and two consumers may be wired (the net then reports no producer). The IO mode does not decide a link: a GPIO (IO3) may feed an analog input (AIN0), which reads the level. A wire may join two pins of one device (board IO3 to board AIN0, an unused pin to its own ground).

**Dependency checks** (`domain/hardware/check.ts`), each issue on the wire or device it is about:
- a wire to a pin the variant no longer brings out, a wire to itself, a duplicate, an incompatible pair;
- per net: a short (two named supplies, or a supply with ground), two unnamed supply outputs, no source, two producers on one net, a consumer with no producer;
- the pin's mode follows the wire: a flexible pin that is the only producer on its net is a note "set it as an output" (one that listens to a producer: "set it as an input"), with a button that adds the pin's default mode (`Output Push-Pull` / `Input`) to the project's setup; the note is gone once the setup holds it;
- voltage: a fixed net (3V3) or a range (VS 6 to 20 V) against each pin's `mV_min` / `mV_max` (overvoltage, undervoltage); a runtime-set rail (REG1, REG2) is a note naming the setting to keep;
- per wired device: a supply pin or ground pin left unconnected, an I2C bus with only one of its two wires.

**Install frame dependency.** A device pin with a csv `cfg_field` (PCA9685 OE = `oe_pin`) wired to a board terminal that is a sys_io pin implies that pin ref in the install frame. A new wire fills the field while it is unset; once it holds something else the panel says "differs" and applies on request, and "matches" when equal. A wire never overwrites a setting silently.

**Canvas.** Press a board terminal or a device pin and drag to another to wire them (refused with the reason shown in the status line); click a wire to select it, Delete removes it; positions are measured from the DOM so wires follow a dragged, rotated or mirrored device. Wire colour is the net's role (power red, ground grey, bus green, signal blue), dashed red with an error. Toolbar options (remembered): Power and IO (off = faded), Device (fade wires not on the selected device), Volts (net voltage label, red on a violation), Available (while dragging, dim the canvas and ring only the pins that can take the wire). Right panel: the selected wire, the selected device's connections and install frame, and the checks of the whole design.

**Assumed or unknown, to be confirmed on the hardware.** `pwmN-pwm` is PCA9685 channel N; the 3V3 / REG / VS terminals follow the art titles; the I2C terminals are GPIO 40 / 41 (`runit_board_defs.h`); GPIO terminals are limited to 3.6 V. Not in the table because nothing states them: the SPI and UART pin numbers, REG1 / REG2 boot voltages (a rail is "set at runtime"), and the motor-driver terminals (not wirable yet). Not yet done: wiring a device by dropping a connector (bundle), connector palette, per-pin wire routing.

## 9c. Board creator as built (2026-10-02)

A board is a **template**; the canvas and the wiring read a **board model** that is either the built-in runIT board or a template. The creator is the Board view's *Edit board* mode (palette left, stage in the middle, properties right, controls in the top toolbar).

**Template** (`domain/board/template.ts`, saved in the project as `board` and in the library as `components/boards/<id>.board.json`): an ID and name, the outline (width, height, corner radius), the mount holes, and the connectors. Units are millimetres, one grid cell is one millimetre. A connector is `{id, type, x, y, rotation, mirror, label, pins[]}`; `x, y` is its centre. The project always carries its own copy; the library is only where new boards start from (the library list is bundled, a save writes the file through the dev server like the component pictures).

**Premade connectors** (`domain/board/connectors.ts`): screw terminals of 1 to 4 pins (5 mm), pin headers 1x2, 1x3, 1x4, 1x6, 1x8, 2x3, 2x4 (2.54 mm), XT60 and USB-C (power: VBUS and GND). Each type has its size, its pin positions and its drawing; the board art is generated from them (`renderBoardSvg`), one `<g id="<connector>-<pin>">` terminal per pin.

**Pin bindings** (the part that makes a connector act like the devices made before, without a body): each pin is *not connected*, a **net** (the nets of the board data: GND, 3V3, REG1, REG2, VS), a **bus signal** (the signals of the board data: I2C SDA / SCL, UART TX / RX, SPI MOSI / MISO / SCK / CS, optionally with its sys pin), or an **installed sys pin** (a provider device and pin: ESP GPIO, the TCA6424A, the ADS7128 analog inputs, the PCA9685 channels, the DAC), with an optional role and maximum voltage for what this board does to the pin (an input behind a divider). Nothing about direction is hardcoded in the editor. A net says in the data whether it can be fed from outside (`input`: only the board input), and only then does the pin get a *Power* choice (provides / takes in; a power connector starts as an input); a bus signal says which way it flows (`signals` in `board-terminals.json`); and the role of a sys pin is derived from the catalog: a pin named in the device's pins csv (`bind_pin`) is what the csv says, otherwise a provider that can set a level, duty or voltage on a pin produces, one that can only read a level or voltage consumes, one that does both is either (the user can override it per pin). What a provider's pins tolerate is data too (`sys_pin_limits`). The resulting role is shown on each pin as a producer / consumer / both flag. The pins the board already uses (I2C, status LEDs, the motor drivers' control pins) are read from the firmware catalog, so a taken pin is marked "used". A bound pin becomes a terminal with the same `PinSpec` the built-in board's terminals have, so wiring, the producer / consumer rules, the checks and the install-frame fill work on it unchanged; wires made for another board are reported as dangling.

**Creator.** *Create a board* or open one from the library; add connectors from the palette (each goes to the free spot nearest the middle); drag to move, snapped to a 0.25 / 0.5 / 1 / 2.54 mm grid; R / M turn and mirror, Delete removes, arrows nudge; select a pin on a connector to edit its label and binding. Checks list structural errors (unknown type, bad ID, repeated connector ID) and warnings (a connector off the board, two connectors overlapping, one sys pin on two pins). Save makes the draft the project's board and writes the library file; *Use runIT board* returns the project to the built-in board.

**Not in this step.** The built-in runIT board stays hand-drawn art with its terminal table; it cannot be opened in the creator (it has no connector list), and its art is drawn at 20 units per "millimetre" so it is smaller than the real board while custom boards and device pictures are in true millimetres. Not yet: decor (MCU, buttons, labels, shapes), connector types beyond the premade set, free rotation, an undo per edit (Save is the undo step), deleting a board from the library.

## 10. Still open

1. Grid unit (deferred, see 7).
2. Variant naming and the default variant when a device has several (first in the list, or flagged in the device json).
3. Where the later saving path goes once the app is deployed (embedded deploy, account storage, mobile PWA, see §9.13). Not needed for v1.

## Original notes (verbatim)

Idea is to make canvas with objects (devices) made of svg elements - semi procedurally generated along with board (pcb), pins, irl connectors like usb, screw terminal goldpins.
Object (devices) will serve three purposes:
- Eaisyfy project configuration via highlitinng suggested availabe pins, power sources and so on - displayting io interconnections and power network along with values and limits
- Enable device creation and hookup visually (semi-configure some of pins values and so on )
- Give an brief lookup on project device - electrical structres.
Everythin should be procedurally generated from .csv json and svg files if provided

on canvas there should be option:
Highlite power networks flow
Highlite io networks
highlite device connections
Highlite voltages
highlite only availabe connections when connecting pin to pin
idea of files is shown in device-pca9685 - no provided svg for this chip - it can use generic template
for example when i will create servo device - there will svg with servo
Wires act as normal electrical connections - servo 3 wires and so on.
All should opereate on created devices and allow to add devices from pallete as usuall. board should be fixed to center and alowed to have placed connectors as in irl board - screw terminal and so on - look ar board-simple svg - you can create pallete of those connectors
- there should be alawys enabled grid so placement of connectors and so on is eaisy - implicating svg image distinct sizes to fit grid
