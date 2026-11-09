# sys_touch — the touch contract

Not a module with state of its own: a set of device ops that mean the same on every touch driver, like the display contract (SYS_DISPLAY.MD). The wrappers in `sys_touch.c` only build the `sys_device_call`. The LVGL adapter (`sys_lvgl`) reads the touch through this contract, not the chip.

A touch panel is one point (LVGL's pointer input is one point and works out its gestures itself). The chips of the displays runIT drives:

| Family | Kind | Bus | Notes |
|---|---|---|---|
| XPT2046 (TSC2046, ADS7846) | resistive | SPI, shared with the display, its own CS and a much lower clock | calibration and a pressure threshold; needs `sys_spi` |
| FT6206 / FT6236 / FT6336 (FocalTech) | capacitive | I2C, 0x38 | |
| CST816S / CST816T / CST820 (Hynitron) | capacitive | I2C, 0x15 | its interrupt is a pulse; it sleeps after about 2 s without a touch and does not acknowledge I2C until touched |
| GT911 (Goodix) | capacitive | I2C, 0x5D or 0x14 | the address is the level of INT during its reset: INT and RESET both needed |

| File | Contents |
|---|---|
| `sys_touch_contract.h` | TOUCH: contract table, `touch_cfg_t`, `touch_point_t`, `touch_calibration_t`, the axes flags, wrappers |
| `contract_touch.json` | The contract's descriptor: POINT, CALIBRATION |
| `errors/sys_touch_errors.h`, `errors/errors_sys_touch.json` | `ERR_SYS_TOUCH_MAP`, the module's error tags, their messages |
| `sys_touch.c` | The error module and the wrappers. `WHOLE_ARCHIVE`: a driver raises `ERR_TOUCH_*` without calling a wrapper |
| `CMakeLists.txt` | Component |
| `AGENTS.md` | Rules for changing this folder |

## TOUCH (`0x0Bxx`)

| Op | Side | Wrapper | Args | Answer |
|---|---|---|---|---|
| `0x0B00` POINT | R | `sys_touch_get_point(dev, &p)` | – | `touch_point_t {u8 pressed, u16 x, u16 y}` |
| `0x0B01` CALIBRATION | RW | `sys_touch_set_calibration(dev, &c)`, `sys_touch_get_calibration(dev, &c)` | `touch_calibration_t {i16 x_min, x_max, y_min, y_max}` | the same |

Both are commands (no pointer, no state of the adapter): the app reads POINT to test a panel and sets CALIBRATION in its calibration wizard. No events.

- **POINT** answers in the display's pixels, **always in the display's orientation 0**: `x` 0 .. `max_x - 1`, `y` 0 .. `max_y - 1`, clamped. LVGL turns the point by the display's rotation itself (`indev_pointer_proc`), so the touch never follows the display's ORIENTATION. While not pressed it answers the last point (LVGL ends a click there). One cheap read: the adapter calls it every LVGL input period (33 ms).
- **Not pressed, not an error**: a chip that sleeps and does not acknowledge (CST816) answers not pressed; so does a suspended device (the registry runs reads while suspended), without touching the bus. A bus that fails while the chip is awake is an error (the bus's, e.g. `ERR_I2C_NACK`).
- **The interrupt line** is the driver's business: a level or a pulse, depending on the chip. The driver may use it (an IO edge of the GPIO device) to read the bus only after a touch; POINT means the same with or without it.
- **CALIBRATION**, resistive panels only: the raw values at the display's edges (min and max may be swapped: an axis read the other way). A capacitive driver's `has_op` leaves it out (`ERR_DEV_UNKNOWN_OP`, not in OPS). A write holds until the next CREATE; the lasting value is the config's (`cal_*`): the app writes CALIBRATION to try it and saves it into the device's config.
- **SUSPEND** puts the chip to sleep, **RESUME** wakes it, **RESET** resets the chip and takes the config's calibration again.

## Create

`touch_cfg_t`:

| Group | Fields | Notes |
|---|---|---|
| Bus | `bus` (an I2C bus of `sys_i2c` or an SPI bus of `sys_spi`, the one the chip is on), `i2c_addr`, `freq_hz` (this chip's clock) | the bus's kind follows from the device type |
| Pins | `cs` (SPI), `irq`, `reset` (`sys_io_pin_t`) | `irq` must be a pin of the ESP32-S3 GPIO device (`ERR_DEV_BAD_PIN`); GT911 needs `irq` and `reset` (it picks its address from INT during the reset: low 0x5D, high 0x14) |
| Range | `max_x`, `max_y` | the display's native width and height (orientation 0); the driver scales the chip's range to it |
| Axes | `axes` (`TOUCH_AXES_SWAP_XY`, `_MIRROR_X`, `_MIRROR_Y`) | how the touch panel lies on the glass, set once; not the screen's rotation |
| Chip | `chip` | the chip of the driver's family (the driver's own list) |
| Resistive | `cal_x_min`, `cal_x_max`, `cal_y_min`, `cal_y_max`, `pressure_min` | the others ignore them |

CREATE resets the chip, reads its ID where it has one (`ERR_TOUCH_BAD_CHIP` for another chip: a wrong address or cable shows at once) and sets its mode.

## What the LVGL adapter does with it (`sys_lvgl`)

- A `lv_indev` of type pointer whose `read_cb` reads POINT; a failed read answers released.
- The `lv_display` is created in the display's native size and its rotation always equals the display's ORIENTATION (at INSTALL, after ROTATION and after the display's RESET): then LVGL's turn of the point matches the panel.
- `max_x`, `max_y` of the touch are the display's native size: the app's configurator fills them from the display the touch belongs to (the adapter cannot read a device's config, so it does not check them).
- A touch that keeps failing is reported once, when it starts failing, not at every read (30 reads a second); a read that works arms the report again.
- Event mode (`lv_indev_set_mode(EVENT)`, `lv_indev_read`) needs the LVGL lock: if it is ever used, the touch interrupt wakes the LVGL task directly, not through the event task.

## Errors (`ERR_MOD_SYS_TOUCH`)

| Tag | Meaning |
|---|---|
| `ERR_TOUCH_BAD_CHIP` | CREATE read another chip's ID than the driver's (or the config's `chip`) |

## Open

- No touch driver yet. XPT2046 needs `sys_spi`.
