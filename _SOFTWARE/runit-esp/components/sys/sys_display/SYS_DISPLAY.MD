# sys_display — the display contract

Not a module with state of its own: a set of device ops that mean the same on every display driver, like the power contracts (SYS_POWER.MD), nothing else. The wrappers in `sys_display.c` only build the `sys_device_call`. The adapter that draws on a display (`sys_lvgl`) knows this contract, not the chip.

A display is a colour panel on **SPI**: ST7789, ILI9341, ST7735, GC9A01, ST7796, ILI9488 (1.3" to 3.5", up to 480×320). No I2C panels (a colour frame at 400 kHz takes seconds); touch is a separate device on its own contract and bus.

| File | Contents |
|---|---|
| `sys_display_contract.h` | DISPLAY: contract table, event, enums, `display_cfg_t`, `display_geometry_t`, `display_window_t`, the op args, wrappers |
| `contract_display.json` | The contract's descriptor: GEOMETRY, BACKLIGHT, ORIENTATION and FLUSH_DONE (FLUSH is local, not a command) |
| `errors/sys_display_errors.h`, `errors/errors_sys_display.json` | `ERR_SYS_DISPLAY_MAP`, the module's error tags, their messages |
| `sys_display.c` | The error module and the wrappers. `WHOLE_ARCHIVE`: a driver raises `ERR_DISPLAY_*` without calling a wrapper |
| `CMakeLists.txt` | Component |
| `AGENTS.md` | Rules for changing this folder |

## DISPLAY (`0x0Axx`)

| Op | Side | Wrapper | Args | Answer |
|---|---|---|---|---|
| `0x0A00` GEOMETRY | R | `sys_display_get_geometry(dev, &g)` | – | `display_geometry_t` (width, height in the current orientation, format) |
| `0x0A01` BACKLIGHT | W | `sys_display_set_backlight(dev, duty)` | `{u16 brightness_duty}` 0 .. 4095 | – |
| `0x0A02` FLUSH | W, **local** | `sys_display_flush(dev, pixels, len, window)` | `display_flush_args_t {const void* pixels, u32 len, display_window_t window}` (C only) | – |
| `0x0A03` ORIENTATION | RW | `sys_display_set_orientation(dev, o)`, `sys_display_get_orientation(dev, &o)` | `{u8 orientation}` (`sys_display_orientation_e`) | the same |

FLUSH carries a pointer to the adapter's pixels, so it is **local** (`SYS_DEVICE_OP_LOCAL` in the contract table, SYS_DEVICE.MD): the device router and a remote device refuse it with `ERR_DEV_LOCAL_OP`, so its args never come from a frame. Only the adapter on this board calls it, one at a time, so a FLUSH_DONE always ends the adapter's last accepted FLUSH. No VM block may have FLUSH: the VM calls `sys_device_call` past the router.

- **FLUSH**: the driver checks `pixels` (not NULL, DMA-capable memory: `esp_ptr_dma_capable`, else `ERR_DEV_BAD_ARG`) and the window (inside the panel, and `w × h × bytes per pixel` of the format equal to `len`, else `ERR_DISPLAY_BAD_WINDOW`), sets the window and **queues** the pixels (CASET, RASET, RAMWR) and returns. It does not wait for the transfer: FLUSH runs under the registry lock (SYS_DEVICE.MD "Concurrency"). One FLUSH in flight (`ERR_DISPLAY_FLUSH_BUSY`). The adapter leaves the pixels untouched until `FLUSH_DONE`.
- **GEOMETRY** is read by the adapter at its INSTALL, to size the draw buffers (the driver does not know them), and after every ORIENTATION. The remote side reads GEOMETRY and sets BACKLIGHT and ORIENTATION.
- **ORIENTATION** turns the picture in the panel itself (the controller's scan direction: MADCTL). The driver works out the RAM offset for the new orientation from the native `offset_x`, `offset_y`, and GEOMETRY answers the new width and height. LVGL draws in PARTIAL mode, where it cannot turn the picture itself without a third buffer; the adapter's ROTATION is this op, then `lv_display_set_rotation`.
- **BACKLIGHT** goes to the IO PWM of the device that has `backlight` (`IO_PWM_DUTY`, 12 bits), inverted when `bl_active_level` is 0. CREATE and RESUME set `brightness_default`; SUSPEND turns it off and sleeps the panel.

### Pixel formats (`sys_display_format_e`)

The format says what the bytes of a buffer are, exactly as the driver sends them; the adapter picks the LVGL format from it and the driver converts nothing.

| Format | Bytes per pixel | LVGL | Panels |
|---|---|---|---|
| `RGB565_SWAPPED` (0) | 2, the high byte first | `LV_COLOR_FORMAT_RGB565_SWAPPED` (`LV_DRAW_SW_SUPPORT_RGB565_SWAPPED`) | ST7789, ILI9341, ST7735, GC9A01, ST7796 |
| `RGB888` (1) | 3: B, G, R; an 18-bit panel takes the 6 high bits | `LV_COLOR_FORMAT_RGB888` | ILI9488 (18 bit only on SPI); the driver sets the colour order so that B, G, R come out right |

| Event | ID | Channel | Flags | Value |
|---|---|---|---|---|
| `FLUSH_DONE` | `0x0A00` | 0 | – | 0, or the error code of the transfer (the `.code` of `ERR_DISPLAY_TRANSFER`) |

Published after the transfer of every **accepted** FLUSH, also after a failed one; never after a FLUSH the driver or the registry refused (`BAD_ARG`, `BAD_WINDOW`, `FLUSH_BUSY`, `SUSPENDED`): those come back from FLUSH as an `err_h`, and the adapter releases LVGL itself. From the transfer-done interrupt the driver calls **`sys_event_publish`**, which hands the whole delivery to the event task; `sys_event_publish_deferred` reads the clock under a lock and is not for an ISR. No NOTIFY: it is for the adapter, not the app. FLUSH is asynchronous: it returns once the driver has queued the pixels, and `FLUSH_DONE` ends it. The value is a code, not an `err_h`: the event holds no pointer, an `err_h` has one owner and the event has any number of listeners. The adapter, in the event task, builds the error from it: `err_report(ERR_NEW(ERR_DISPLAY_TRANSFER, .id = ev->source, .code = ev->value))`. On SPI the transfer itself reports no error (the bus's done callback has no status), so the value is 0 and a failure comes back from FLUSH.

## Create

`display_cfg_t`, every difference of the glass in the config (the same firmware on every board):

| Group | Fields | Notes |
|---|---|---|
| Bus | `bus` (sys_spi), `spi_mode` (0..3), `freq_hz` | a module without CS often needs mode 3 |
| Pins | `cs`, `dc`, `reset`, `backlight` (`sys_io_pin_t`) | `cs` and `dc` must be pins of the ESP32-S3 GPIO device (switched in every transaction), else `ERR_DEV_BAD_PIN`; `dc` is required; the rest `SYS_IO_PIN_NONE` when unused |
| Backlight | `bl_active_level`, `brightness_default` | |
| Glass | `width`, `height` (native, orientation 0), `offset_x`, `offset_y` (where the glass starts in the controller's RAM), `rgb_order` (`sys_display_rgb_order_e`), `invert` (most IPS glass) | |
| Controller | `panel` (the driver's init sequence), `orientation` (at start) | |

## What the LVGL adapter must do (`sys_lvgl`)

- Draw buffers `MALLOC_CAP_DMA` in internal RAM (a buffer elsewhere is copied by the SPI driver in every transaction), 4-byte aligned, at most 32 KB each (one SPI DMA transaction on the ESP32-S3); the bus's `max_transfer_sz` at least the buffer.
- LVGL in PARTIAL mode, `LV_DRAW_BUF_STRIDE_ALIGN` 1 (rows without padding: `w × h × bpp` contiguous bytes).
- `flush_cb` calls FLUSH with `px_map` (the start of buffer 1 or 2), `len` = the area's bytes and the area. A FLUSH that fails releases LVGL at once (`lv_display_flush_ready`) and invalidates the screen.
- A `flush_wait_cb` that waits on a semaphore the FLUSH_DONE listener gives (subscribed on the device id). Without it LVGL spins in `while(disp->flushing);` holding its lock. A FLUSH_DONE with nothing in flight is ignored.

## Errors (`ERR_MOD_SYS_DISPLAY`)

| Tag | Meaning |
|---|---|
| `ERR_DISPLAY_BAD_WINDOW` | The window is empty, outside the panel, or its bytes are not the FLUSH's `len` |
| `ERR_DISPLAY_FLUSH_BUSY` | A FLUSH is still in flight |
| `ERR_DISPLAY_TRANSFER` | The bus transfer failed |

## Open

- `sys_spi`: the firmware has no SPI bus yet; a display driver needs it.
- No display driver yet, and no `sys_lvgl`.
