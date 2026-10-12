# DAC53202 — dual 10-bit voltage DAC

Type `0x0046`. On the runIT board: bus 0, address 0x48 (A0 = GND); OUT0 / OUT1 are the VREF of DRV8962 #1 / #2 (the chopping current). Both channels use VDD as the reference at gain 1x: full scale 3.3 V (`DAC53202_VREF_MV`).

## Create

`[05][id u16][00 00][46 00][bus][addr]` — `dac53202_cfg_t {u8 i2c_bus, u8 i2c_addr}`. The bus is 0 or 1 and the address 0x48..0x4B (A0 picks one of four), else `ERR_DEV_RANGE`. `create()` probes the chip, selects the VDD reference on both channels and leaves both outputs powered down (Hi-Z) until a voltage is set.

## Ops

| Op | Args | Answer | Notes |
|---|---|---|---|
| `0x0001` RESET | – | – | Both outputs Hi-Z, set voltages forgotten (refused while suspended) |
| `0x0002` SUSPEND | – | – | Both outputs Hi-Z; the channels that were on are remembered; writes refused until RESUME. The device counts as suspended even if the write failed |
| `0x0003` RESUME | – | – | The remembered channels come back with their last code. A failing resume leaves it suspended |
| `0x0106` IO RESET | `{u8 pin}` | – | The channel Hi-Z, its voltage forgotten |
| `0x0108` IO GET_VOLTAGE | `{u8 pin}` | `i32` mV | The last set voltage from its code (so the 10-bit rounding shows), 0 for an off channel. A read: allowed while suspended |
| `0x0109` IO SET_VOLTAGE | `{u8 pin, i32 mV}` | – | 0..3300, else `ERR_DEV_RANGE`; the code is written first, then the channel powered up if it was down |

Pin ≥ 2 → `ERR_DEV_BAD_PIN`. The data register is a 10-bit code, MSB left-aligned in 16 bits (datasheet 7.6.8: 0x155 is written 0x5540); one step is 3.2 mV. The channels are not lockable (no PINS contract: nothing locks a DAC output).

## Changes from legacy

- Voltage maths in 10-bit codes (legacy wrote a 12-bit code shifted left by 4, so the chip took the top 10 bits of it): the read-back is exact to the step.
- RESET and the IO RESET also forget the voltage (legacy kept the old code, so a read after a power-down showed a voltage that was not there).
- `ERR_DEV_RANGE` and `ERR_DEV_BAD_PIN` instead of the legacy checks.

## Tested

2026-10-11 on the runIT PCB (`scripts/board_tests.py`): create errors, set / read back 0 / 1000 / 2500 / 3300 mV within one step, ranges, bad pin, suspend / resume, reset. The output voltage itself was not measured.
