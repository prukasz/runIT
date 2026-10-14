# PCA9685 — 16-channel PWM expander

Type `0x0041` (u16; answered by INFO). On the runIT board: bus 0, address 0x60, OE on TCA6424A pin 0. CH 0–7 are the servo headers; CH 8–15 drive DRV8962 #2 and must not be offered as free PWM.

## Create

The type registers itself (`SYS_DEVICE_TYPE_DEFINE`, `SYS_DEVICE_TYPE_PCA9685`); the header exports only the create struct and the limits. CREATE:

`[05][id u16][00 00][41 00][bus][addr][oe: u16 device, u8 pin]`

`create()` probes the chip, wakes it, enables register auto-increment, makes OE an output, locks it on its device (PINS contract) and drives it low. The OE device must therefore answer SET_MODE and the PINS ops. The registry allocates the struct and adds the instance; on a failure it calls `uninstall()` and wraps the error in `ERR_DEV_INSTALL{id, 0x0041}`.

| Field | Type | Notes |
|---|---|---|
| `i2c_bus` | u8 | 0 or 1, else `ERR_DEV_RANGE` |
| `i2c_addr` | u8 | 7-bit; probed (`ERR_I2C_DEV_NOT_FOUND`) |
| `oe_pin` | `sys_io_pin_t` {u16 device, u8 pin} | `SYS_IO_PIN_NONE` = none. Its device must exist (`ERR_DEV_NOT_FOUND`); it can't be removed while the PCA9685 exists (`ERR_DEV_IN_USE`). Driven through the IO contract |

The PWM frequency stays at the chip's default until SET_PWM_FREQ.

## Ops

`[0x05][id u16][op u16][args]` (op little-endian: `0x0103` is sent `03 01`).

| Op | Args | Answer | Notes |
|---|---|---|---|
| `0x0001` RESET | – | – | All channels off |
| `0x0002` SUSPEND | – | – | OE high, chip asleep (every step runs); writes refused until RESUME |
| `0x0003` RESUME | – | – | Chip awake, OE low |
| `0x0004` REMOVE | – | – | Generic (registry). `uninstall()`: chip asleep, I2C address released, OE high |
| `0x0005` INFO | – | `{u16 0x0041, u8 suspended}` | |
| `0x0006` OPS | – | `u16[]` | The ops above, from the op table (generic) |
| `0x0100` SET_LEVEL | `{u8 pin, u8 level}` | – | Full off / full on |
| `0x0101` TOGGLE | `{u8 pin}` | – | |
| `0x0102` GET_LEVEL | `{u8 pin}` | u8 | From the last written duty (≥ half = 1); allowed while suspended |
| `0x0103` SET_PWM_DUTY | `{u8 pin, u32 duty}` | – | 0..4095, else `ERR_DEV_RANGE` |
| `0x0104` SET_PWM_FREQ | `{u8 pin, u32 Hz}` | – | 24..1526 Hz, one frequency for all channels (`pin` ignored) |

Pins contract (SYS_IO.MD): `0x0200` AVAILABLE (always `0xFFFF`), `0x0201` LOCKED, `0x0202` LOCK, `0x0203` UNLOCK, `0x0204` SET_LOCKED_LEVEL. IO writes to a locked channel → `ERR_DEV_PIN_LOCKED`. The driver reserves nothing: all 16 channels are free until another device locks some (on the runIT board the DRV8962 locks CH 8–15) and drives them with SET_LOCKED_LEVEL. RESET and SUSPEND still turn every channel off, locked or not.

Pin ≥ 16 → `ERR_DEV_BAD_PIN`. Any other op → `ERR_DEV_UNKNOWN_OP`.

## Tested

2026-10-10: CREATE error paths on a board without the chip: no type, unknown type, size, ID 0xFFFF, bus, pin order, no chip, nothing left registered. The chip part of `router_tests.py` creates ID 3 at bus 0 / 0x60 and then runs `test_pca`; not run yet.
