# vm_blocks — the block palette (for agents)

Component `components/vm/vm_blocks/` (ported 2026-10-11 from `legacy/components/VM/blocks`). The block types the VM can run. The core (`vm_core`, VM_CORE.MD) knows no block: it looks a `block_type` up in the palette and calls what it finds. The dependency runs blocks → core, never back.

## Layout

```
core/                  the logic blocks: expr_core/ and branch_core/ (shared), expr, expr_bit, if, switch, for, set,
                       clone, edge, timer, latch, periodic, action, on_event
device-contract/       one block per device contract (io, hbridge, power_monitor, power_vreg, power_supply,
                       power_usb_pd) and the engine they share (vm_block_device.h/.c)
device-custom-block/   a block for the own operations of one device type (servo, drv8962, tps55289), on the same engine
```

Every block folder holds `vm_block_<name>.h/.c`; all folders are on the include path (`CMakeLists.txt`), so includes are by bare file name.

## What is here

| Block | Type | Pins (in / out / enables) | Notes |
|---|---|---|---|
| EXPR | 1 | any / 1 / – | float RPN formula; `expr_core/` holds the shared evaluator |
| EXPR_BIT | 2 | any / 1 / – | u32 bitwise formula (same core) |
| IF | 3 | 1 / 2 / – | routes by a condition; outputs are gates; `branch_core/` shared with SWITCH |
| SWITCH | 4 | 1 / 1..n / – | routes by an index |
| FOR | 5 | 0..3 / 0..1 / – | loop over a span of following blocks |
| SET | 6 | 2 / 0 / – | copy source → destination, or write a constant |
| CLONE | 7 | 2 / 0 / – | clone a heap object into a cell |
| EDGE | 8 | 0 / 0 / ≥1 | one-pass pulse on the enable's edge |
| TIMER | 9 | 0..1 / 0..1 / – | TON / TOF / TP and inverted variants |
| LATCH | 12 | 0..1 / 0 / ≥1 | SR / RS bistable |
| PERIODIC | 13 | 0..1 / 0 / – | "every N" tick on ENO |
| ACTION | 14 | 0..1 / 0 / – | on the enable's rising edge queues a record of the store by its u32 ID (`sys_actions_request`); the ID from the pin or the state |
| IO | 32 | 0..3 / 0..1 / – | the IO contract (SYS_IO.MD): pins. Device blocks below |
| HBRIDGE | 33 | 0..3 / 0..1 / – | the HBRIDGE contract: motor channels |
| POWER_MONITOR | 34 | 0..3 / 0..1 / – | the POWER_MONITOR contract: voltage, current, power of a channel, alerts |
| POWER_VREG | 35 | 0..1 / 0 / – | the POWER_VREG contract: enable, voltage, current limit of a regulator |
| POWER_SUPPLY | 36 | 0..1 / 0..1 / – | the POWER_SUPPLY contract: select a mux source, read which is on |
| POWER_USB_PD | 37 | 0..2 / 0..2 / – | the POWER_USB_PD contract: ask the charger for a voltage / current, read its highest offer |
| SERVO | 39 | 0..1 / 0 / – | the servo device's own ops (no contract): position, home, release, trim |
| DRV8962 | 40 | 0..2 / 0..2 / – | the DRV8962's own ops: current limit, clear fault, read fault (fault, reason) |
| TPS55289 | 41 | 0..4 / 0..4 / – | the TPS55289's own ops: set / read the protection (four values) |
| ON_EVENT | 15 | 0 / 0..1 / – | ENO pulses and VALUE is set when this pass holds an event matching source / event / channel (`sys_event_matches`, wildcards as in a subscription) |

Numbers are in `vm_core/block/vm_block_ids.h` and never change. The legacy device blocks (10, 11, 16–20) are gone: the device blocks are **one per contract** (below).

### Device blocks (`device-contract/`, `device-custom-block/`)

One block type per device contract, one engine (`device-contract/vm_block_device.c`) behind all of them; each contract has its own folder with its table (op ID, kind, packed argument fields) and its `VM_BLOCK_TYPE_DEFINE` (`device-contract/io/vm_block_io.c` and so on). The block's state (`vm_block_device_data_t`, 56 B) holds `device_id` (u16), `op` (the operation of the contract), `idle_op`, the constants `k[4]` and `idle_k[4]`.

- **Inputs = the operation's argument fields**, in the order of the contract's argument struct, without the device and the op: `IO.SET_LEVEL` = `(pin, level)`, `HBRIDGE.SET_DRIVE` = `(channel, drive in percent)`, `PMON.GET_CURRENT` = `(channel)`. A field with no wire uses its constant `k[i]`; the device range-checks. A field with a **scale** in the table takes a float in the block's unit and multiplies it (HBRIDGE `SET_DRIVE`'s drive is **percent**, -100..100: x40.95 to the wire's 12-bit PWM steps, so 0.025 % is the finest step); the constant of such a field is stored as a float, the unit is the same on both. A wired value is converted to the field's type (rounded, saturated). The load check wants `in_cnt <= fields of the op`, `q_cnt <= 1` (a read's result; 0 for a setting).
- **Kinds of operation:** CHANGE (a setting: sent on the first enabled pass and when an argument changed), EDGE (toggle, brake, coast, reset: once per rising enable), READ (asked on every enabled pass, written to output 0; gate it with PERIODIC).
- **Idle:** while not enabled the block may send one idle operation of the same contract (`idle_op`, 0 = hold); the address field (pin / channel) is the one last sent, the rest `idle_k`. This replaces the legacy "when not active": IO `SET_LEVEL` with `idle_k[0]` 0 / 1 (force low / high), HBRIDGE `BRAKE` or `COAST`.
- **ENO:** CHANGE / READ: true while the device takes the calls; EDGE: pulses on the pass it fired. A failed call is reported (`BLOCK_CALL`) and clears ENO.
- **Per contract:** `naddr` in the contract's table says how many leading fields address a thing (a pin, a channel: 1 for IO / HBRIDGE / POWER_MONITOR, 0 for a regulator, a mux, a USB-PD sink); the idle operation reuses those fields as last sent. A result is one output (u8 or i32) or, for `POWER_USB_PD.GET_LIMITS`, two (voltage, current). The device `0xFFFF` (none) is refused.
- **A device's own ops:** a device-specific block is the same engine with that device's table (`device-custom-block/servo/`: `SET` position as a float in degrees / percent, x10 to the wire's tenths; `HOME`, `RELEASE` as EDGE or as the idle operation; `SET_TRIM`; no output). It depends on the device's header (`device_servo` in the component's REQUIRES), unlike the contract blocks. `drv8962/`: `SET_CURRENT_LIMIT` (channel, mA), `CLEAR_FAULT`, `GET_FAULT` -> outputs fault, reason. `tps55289/`: `SET_PROTECTION` (four inputs), `GET_PROTECTION` -> four outputs. A result of several values fills the outputs in the order the device answers.
- **Not in the tables:** IO `CONFIGURE_INTR` (a setup struct), `POWER_USB_PD.LIST_OPTIONS` (an array answer), the PINS contract (install time), the EVENT contract (the ON_EVENT block) and POWER_MANAGER (events only), the device lifecycle (reset, suspend, resume, remove, create: a program does not manage devices; the router does, number 38 stays unused). A device's own operations (`0xFFxx`, e.g. the servo's) need a generic `DEVICE_CALL` block, not written yet.
- **Adding a contract:** a folder `device-contract/<contract>/` with `vm_block_<contract>.h/.c`: a table and a `VM_BLOCK_TYPE_DEFINE` over `vm_device_run` / `vm_device_check`, a number in `vm_block_ids.h`; a `_Static_assert` per row ties the fields to the contract's argument struct. Not run on a board; the legacy `allowed_mask` pin list is gone (the device answers `ERR_IO_PIN_UNAVAILABLE`).

## Adding a block

1. A folder `core/<name>/` (a device block: `device-contract/` or `device-custom-block/`) with `vm_block_<name>.h` (state struct, pin enums; no annotations, the app's description is the `block_<name>.json` next to it, see "Descriptors" below), `vm_block_<name>.c` (the body `vm_blk_<name>(vm_block_h)`, the optional `vm_verify_<name>`) and `<name>.md` (the user guide).
2. A number in `vm_block_ids.h`.
3. At the end of the `.c`: `VM_BLOCK_TYPE_DEFINE(VM_BLK_<NAME>, .run = …, .check = …, .min_in = …, .max_in = …, .min_q = …, .max_q = …, .min_en = …, .required_in = …, .state_len = sizeof(…))`. Unset fields are 0. It registers the type before `app_main` (a bad, zero or repeated id, or no body, aborts the boot with the id in the log).
4. The file in `CMakeLists.txt` (`SRCS`, `INCLUDE_DIRS`). The component is `WHOLE_ARCHIVE`, so nothing else references the block.
5. `block_<name>.json` next to the header (schema `templates/vm-block.schema.json`), then `sh templates/sync.sh`: it validates the descriptor, checks its references (contract, device, `wire_id` against `vm_block_ids.h`) and copies it to `generated/blocks/`.

`vm_block_verify` enforces the pin shape and the state size at load (`ERR_VM_BLK_BAD_SHAPE`), then calls `.check`; bodies never re-check their shape.

## Descriptors for the app

The headers carry no annotations. Each block is described by a `block_<name>.json` in its folder, written by hand against `templates/vm-block.schema.json` (every user-facing name is an alias; see `templates/AGENTS.md`). `templates/sync.sh` validates it and publishes it to `generated/blocks/`, which the app reads. Every block type has one (`templates/crosscheck.py` lists any that does not). `expr` and `expr_bit` name an `editor` the app provides: their formula and bytecode are not in the descriptor.

## Not yet ported

- Blocks for the own operations of the other devices (PCA9685, INA3221, AP33772S, ...), when a program needs them: a folder in `device-custom-block/` with a table, as the three there.

## Tests

`.claude/skills/runit-esp/scripts/vm_suite.py` (PCB, 2026-10-12), see VM_CORE.MD "Tests". Per block: every shape rule at load, and the behaviour in scan mode (one pass per step): all 50 EXPR and 23 EXPR_BIT operations with their faults, IF / SWITCH (types, rounding, 16 cases, enables ANY / ALL), FOR (all operations and comparisons, pins, budget, nesting depth, bad spans), SET (all types, arrays, trees, constant mode with saturation, refusals), CLONE (heap object, refill, reshape), EDGE, LATCH, TIMER (six modes, units, pin preset, a pause is not counted), PERIODIC (rate, pin, restart, overrun), ACTION (records, id pin, no enable), ON_EVENT (wildcards, one pass, missed while disabled), and the IO and POWER_MONITOR blocks on real chips. The motor, servo and regulator blocks are only loaded (shape), never started.
