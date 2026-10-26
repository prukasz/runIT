# The hardware library

`library/parts/<id>/` holds the physical things of the Devices canvas (runit-app `src/app/hardware/`): boards, modules and devkits, chips, actuators, sensors, passive and power parts. Each part is two files: `part.json` (follows `templates/part.schema.json`) and its drawing `part.svg`. The app bundles every part (`bundledLibrary()`, alias `@library`); nothing else is hard-coded in it except the standard connectors it draws (goldpin headers, female headers, screw terminals, wire leads, XT60, USB-C, pads). `templates/check_library.py` (run by `templates/sync.sh`) validates them.

## A part

| Field | What |
|---|---|
| `id`, `name`, `category`, `description`, `tags` | The folder name, what the palette and the canvas call it, its palette group (`board`, `module`, `chip`, `actuator`, `sensor`, `passive`, `power`) |
| `size_mm` | Its real size; `part.svg` has `viewBox="0 0 w h"` in millimetres. The canvas's unit is the millimetre, so every part is to scale against the others |
| `graphic` | `part.svg`: the body only. Connectors and chips are not drawn in it: the app draws them from `connectors` and `chips`, at their true size |
| `board` | A board part: the `board.json` it draws (`name`, `revision`). The canvas shows the part whose name and revision match the app's board |
| `device` | The device the part is (`type`: a device descriptor's id, `preset`: one of its presets, `config`: values over the preset). Placing the part adds that project device with the user bus, the preset's values and `config` |
| `nets` | Its power nets. `role`: `sink` (it takes power: `voltage_mV` min / max / nominal, `current_mA` typical / peak), `source` (it gives power: driven by an element, or an external supply such as a battery), `through` (it only passes power between its pins: a breakout's V+), `internal` (a board's input: USB VBUS, the XT60; a supply is wired there, never a part that draws). `max_mA` what it carries, `monitor` the power monitor channel that measures it |
| `connectors` | Standard connectors: `type`, `at` (the centre of the first pin, mm), `rotation` (0: pins run left to right, 90: down), `pitch_mm` (a screw terminal: 3.5, 5.0 or 5.08; goldpins are always 2.54), `rows` (a goldpin header), `from` (a wire lead: where it leaves the body, as on the real part: a servo's cable from the middle of its short side, away from the shaft; it leaves straight out of that side; `at` is its plug while nothing is wired to it: wired, the canvas lays the lead towards its wires, its plug as far from `from` as `at` is). A `usb_c` socket takes a cable, not wires: its pins name what it brings (VBUS's net for the power, the data lines) and the canvas wires nothing to them |
| `connectors[].pins[]` | `id`, `label` and what it is, one of: `gnd`; `net` (a power net); `device_pin` (`{device, pin}`: a pin of a board device by its id, or of the part's own device, `"self"`); `config` (a pin field of the part's device: the pin it is wired to goes there, a servo's `pwm_pin`); `bus` (`{type, line, bus?}`: a board's bus line has its bus number, a part's has none and its device's bus follows the wire); `hbridge` (`{device, channel, supply}`: an H-bridge output, OUT1 is channel 0, powered from the net `supply`); `motor` (`a` / `b`: a motor lead; its draw is the part's sink net); `nc` |
| `power.elements` | A board's (or module's) flow of energy between its nets: `source` (energy in: `voltage_mV`, `levels_mV` for USB PD, `max_mA`), `fuse` (an eFuse `in` → `out`), `regulator` (`in` → `out`, `voltage_mV` with `step`, `adjustable`: the board sets it with POWER_VREG, `efficiency_pct`), `mux` (`in`: its inputs, `inputs`: their names, `out`), `load` (what the part itself draws from `in`). `device` is the board device that is the element. `power.limits.max_mA` is the whole input's limit |
| `chips` | Chips on a board (the PCB creator): a labelled block at `at` / `size_mm`, or a library part's drawing when `type` is a part id (the ESP32-S3 module), optionally the board device it is |

## What the canvas does with a part

Pins are joined by wires, and inside a part by a common net (its power net, its grounds). The rules of a wire (`wiring.ts`): ground to ground; a sink to a supply whose voltage fits (an adjustable regulator: what it can be set to for everything on it), never two supplies together, never a sink onto a board input; a `config` pin to a free `device_pin` whose device's pins take the mode the field `requires` (not a reserved pin, not one another device uses); bus line to the same line; a motor's two leads to the two outputs of one bridge, or one lead to ground. Picking a pin lights every pin it may go to.

What the wires decide is written into the project (`apply.ts`): the config pin of the part's device, its I2C bus, and for the board's power the start commands of the regulators it sets (POWER_VREG VOLTAGE and ENABLE) and of the muxes it picks (POWER_SUPPLY SOURCE). The power solver (`power.ts`) carries what the sinks accept up through muxes and eFuses to the source, picks each mux's input, each adjustable regulator's voltage (5 V when all its sinks take it, else the lowest `nominal` all its sinks accept) and the USB PD level (the lowest that fits and carries the load), then carries the current back up and lists what is over a limit or out of range. The user may set the source, its voltage, a mux's input or a rail's voltage by hand (the board's card on the canvas).

## The parts here

| Part | What |
|---|---|
| `runit_board_r1` | The runIT board, 86.25 × 140 mm. Its connectors are where the drill file of `_HARDWARE` (Project Outputs) has their holes; its drawing is `runit-app/board-simple.svg` at 0.14 mm per unit without that file's connectors, their housings and labels (the connectors' labels are drawn at their true pins); REG1 is Rail B, REG2 Rail A (and the servo headers, CH0 at the top), the XT60's + is its chamfered pin; its devices, buses and rails are board.json's |
| `servo_sg90`, `servo_mg996r`, `servo_fs90r` | Servo models (micro, standard, continuous): the servo device with its preset, the voltage they want and what they draw, a 3-wire lead |
| `motor_dc_tt`, `motor_dc_gear_12v` | DC gear motors: the DC motor device with its preset (`tt_6v`, `gear_12v`), two leads to one H-bridge (or one to an output and one to ground: a half bridge, one way); the canvas sets the device's driver and channel from them |
| `pca9685_breakout` | A PCA9685 16-channel breakout: I2C, OE, logic VCC, V+ passed to the 16 servo headers |
| `esp32s3_wroom1` | The ESP32-S3-WROOM-1 module with its pads (a chip for the PCB creator) |

## Making a part

The part creator (the pen on the Devices canvas, or New part in its palette) makes any of these: a name, a kind, the device and its preset, a body (a shape, or a photo whose background is clicked away and which is traced into a few flat colours at the real width), power nets, connectors dragged into place (snapped to a quarter of 2.54 mm), each pin given its role from one list, and on a board its flow of energy and chips. **Use in this project** keeps the part in the project file (it travels with it); **Save to the library** writes `library/parts/<id>/` from the desktop app run from the repository (`npm run app`), the user's own library folder from the installed app, or downloads the two files in a browser. A board part whose `board` names the app's board.json is the canvas's drawing of it (a project part over a library one).
