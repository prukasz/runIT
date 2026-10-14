# Power mux — two switched supply sources into one load

Type `0x004A` (u16; answered by INFO). On the runIT board: one mux per DRV8962 H-bridge VM node, two LM73100 ideal diodes each: source A = the board input (VSUP), source B = the regulator rail of that bridge; the enables are TCA6424A pins (DEVICE_TCA6424A.MD). Port of `legacy/components/devices/device_power_mux` (2026-10-11). **Builds; not run** (the board config doesn't create the muxes yet; `router_tests.py` `test_power_mux` is written, not run).

The part has no bus: each source switch is enabled by a pin of another device. Contract: POWER_SUPPLY (SYS_POWER.MD). At most one source is on; a change switches the old one off first, waits the switch delay, then switches the new one on, so the two are never on together.

## Create

`[05][id u16][00 00][4A 00][en_a: u16 device, u8 pin][en_b: u16 device, u8 pin][active_level][switch_delay_ms]`

| Field | Type | Notes |
|---|---|---|
| `en_a_pin` | `sys_io_pin_t` | Switches source A on when at the enable level. Required (`SYS_IO_PIN_NONE`: `ERR_DEV_BAD_PIN`). Its device must answer SET_MODE, SET_LEVEL, the PINS ops |
| `en_b_pin` | `sys_io_pin_t` | Same for B. Must differ from `en_a_pin` (one pin for both could switch the two sources on together): `ERR_DEV_BAD_PIN` |
| `active_level` | u8 | 0 / 1: the level that switches a source on (1 for the LM73100), else `ERR_DEV_RANGE` |
| `switch_delay_ms` | u8 | 1..100 (`POWER_MUX_*_SWITCH_DELAY_MS`), else `ERR_DEV_RANGE` |

Steps: both pins' devices declared (`sys_device_use`: `ERR_DEV_NOT_FOUND`, and no REMOVE of them while the mux exists), then for each pin: output, driven to "off", locked on its device (PINS). Both sources are off after CREATE; nothing is fed until SELECT. On a failure the registry calls `uninstall()` (each built step released) and wraps the error in `ERR_DEV_INSTALL{id, 0x004A}`.

## Ops

| Op | Args | Answer | Notes |
|---|---|---|---|
| `0x0001` RESET | – | – | Both sources off, nothing wanted |
| `0x0002` SUSPEND | – | – | Both enable pins to "off" (every step runs); the wanted source is kept |
| `0x0003` RESUME | – | – | The wanted source again. A failing resume leaves both off and the device suspended |
| `0x0004..8` | generic | | REMOVE (`uninstall()`: pins to "off", unlocked, reset), INFO, OPS, error policy |
| `0x0800` SELECT | `{u8 source}` 0 off / 1 A / 2 B | – | `ERR_DEV_RANGE` above 2, `ERR_DEV_SUSPENDED` while suspended. The wanted source is stored even if the switch fails |
| `0x0801` GET_SOURCE | – | u8 | What is switched on now (allowed while suspended) |

## Notes

- **The switch delay is a wait inside an op** (`vTaskDelay`, at most 100 ms, under the registry lock): the second bounded wait next to TPS55289's EN start-up (SYS_DEVICE.MD "Concurrency"). It is the hardware's switching time, not a long sequence, so it is not a timer step.
- A failure while switching leaves both sources off (`GET_SOURCE` = 0).
- The choice is a default setting of the app (the stored hardware config applies it after the creates); there is no VM block.

## Changes from legacy

- `device_id` is no longer in the create struct (the registry has it); type `0x4A` → `0x004A` (u16); pins are `sys_io_pin_t` (u16 device).
- The contract is a table (`SUPPLY_*`, `0x08xx`) instead of the `sys_power_supply_contract_t` vtable; packets `0x3A` / `0x3B` are the generic device call.
- Pin errors: `ERR_DEV_BAD_PIN` instead of `ERR_IO_PIN_UNAVAILABLE` / `ERR_IO_PIN_ALREADY_IN_USE`; the source is range-checked with `ERR_DEV_RANGE`.
- RESET and SELECT are refused while suspended (legacy had no write guard).
- The `//#device` / `//#contract` annotations for the app are not carried over yet (SYS_DEVICE.MD "Not yet").
