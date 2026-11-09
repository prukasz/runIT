# sys_display — the display contract

Not a module with state of its own: a set of device ops that mean the same on every display driver, like the power contracts (SYS_POWER.MD), nothing else. The wrappers in `sys_display.c` only build the `sys_device_call`. The adapter that draws on a display (`sys_lvgl`) knows this contract, not the chip.

A display is a colour panel on **SPI**: ST7789, ILI9341, ST7735, GC9A01, ST7796, ILI9488 (1.3" to 3.5", up to 480×320). No I2C panels (a colour frame at 400 kHz takes seconds); touch is a separate device on its own contract and bus.

| File | Contents |
|---|---|
| `sys_display_contract.h` | DISPLAY: contract table, event, enums, `display_cfg_t`, `display_geometry_t`, `display_window_t`, the op args, wrappers |
| `contract_display.json` | The contract's descriptor: GEOMETRY, BACKLIGHT, HEALTH (FLUSH and ORIENTATION are local, not commands) |
| `errors/sys_display_errors.h`, `errors/errors_sys_display.json` | `ERR_SYS_DISPLAY_MAP`, the module's error tags, their messages |
| `sys_display.c` | The error module and the wrappers. `WHOLE_ARCHIVE`: a driver raises `ERR_DISPLAY_*` without calling a wrapper |
| `CMakeLists.txt` | Component |
| `AGENTS.md` | Rules for changing this folder |

## DISPLAY (`0x0Axx`)

| Op | Side | Wrapper | Args | Answer |
|---|---|---|---|---|
| `0x0A00` GEOMETRY | R | `sys_display_get_geometry(dev, &g)` | – | `display_geometry_t` (width, height in the current orientation, format, `flush_max`) |
| `0x0A01` BACKLIGHT | RW | `sys_display_set_backlight(dev, duty)`, `sys_display_get_backlight(dev, &duty)` | `{u16 brightness_duty}` 0 .. 4095 | the same: the duty now, 0 while suspended |
| `0x0A02` FLUSH | W, **local** | `sys_display_flush(dev, pixels, len, window, done, ctx)` | `display_flush_args_t {const void* pixels, u32 len, display_window_t window, done, done_ctx}` (C only) | – |
| `0x0A03` ORIENTATION | RW, **local** | `sys_display_set_orientation(dev, o)`, `sys_display_get_orientation(dev, &o)` | `{u8 orientation}` (`sys_display_orientation_e`) | the same |
| `0x0A04` HEALTH | R | `sys_display_get_health(dev, &h)` | – | `display_health_t {u8 health, u8 power_mode}` (`sys_display_health_e`) |

FLUSH carries pointers (the adapter's pixels, its callback), so it is **local** (`SYS_DEVICE_OP_LOCAL` in the contract table, SYS_DEVICE.MD): the device router and a remote device refuse it with `ERR_DEV_LOCAL_OP`, so its args never come from a frame. Only the adapter on this board calls it, one at a time. No VM block may have FLUSH: the VM calls `sys_device_call` past the router.

- **FLUSH**: the driver checks `pixels` (not NULL, DMA-capable memory: `esp_ptr_dma_capable`, else `ERR_DEV_BAD_ARG`) and the window (inside the panel, and `w × h × bytes per pixel` of the format equal to `len`, `len` at most `flush_max`, else `ERR_DISPLAY_BAD_WINDOW`), sets the window and **queues** the pixels (CASET, RASET, RAMWR) and returns. It does not wait for the transfer: FLUSH runs under the registry lock (SYS_DEVICE.MD "Concurrency"). One FLUSH in flight (`ERR_DISPLAY_FLUSH_BUSY`). The adapter leaves the pixels untouched until `done`.
- **GEOMETRY** is read by the adapter at its INSTALL, to size the draw buffers (the driver does not know them), and after every ORIENTATION. `flush_max` is the largest `len` the driver takes: its bus's transfer size (`max_transfer_sz`), at most 32 KB (one SPI DMA transaction on the ESP32-S3). The remote side reads GEOMETRY and HEALTH, sets and reads BACKLIGHT.
- **ORIENTATION** turns the picture in the panel itself (the controller's scan direction: MADCTL). The driver works out the RAM offset for the new orientation from the native `offset_x`, `offset_y`, and GEOMETRY answers the new width and height. LVGL draws in PARTIAL mode, where it cannot turn the picture itself without a third buffer, and `lv_display_set_rotation` only swaps its width and height. So ORIENTATION is **local**: only the adapter turns the panel, so LVGL always draws for the panel as it is (the app turns the screen with the adapter's ROTATION). The adapter's ROTATION waits for the last `done`, calls ORIENTATION, then `lv_display_set_rotation`, and invalidates the screen. The driver refuses ORIENTATION while a FLUSH is in flight (`ERR_DISPLAY_FLUSH_BUSY`): MADCTL never changes in the middle of a frame.
- **BACKLIGHT** goes to the IO PWM of the device that has `backlight` (`IO_PWM_DUTY`, 12 bits), inverted with `SYS_DISPLAY_FLAG_BL_ACTIVE_LOW`. CREATE clears the panel's RAM before it turns the picture on (DISPON), so no noise shows; CREATE and RESUME set `brightness_default`. The adapter never sets BACKLIGHT: it is the app's (remote) and the VM's. SUSPEND turns it off (its read answers 0) and sleeps the panel. SUSPEND does not cut a transfer short: the panel's sleep command waits for the queued transfer (`esp_lcd`'s `tx_param` does), and its `done` still comes.

- **HEALTH**: whether the panel is still there. SPI has no acknowledge, so a cable that came off goes unseen by FLUSH; only a read from the panel tells. With `SYS_DISPLAY_FLAG_READABLE` (the module's SDO wired to the bus's MISO, which `sys_spi` pulls up) the driver reads the power mode (MIPI DCS RDDPM, 0x0A): `0xFF` or `0x00` is `NO_ANSWER` (nothing drives MISO: the cable or the module's power is off); a panel that answers but sleeps or shows nothing while the driver set it running is `RESTARTED` (it lost its power and came back in its power-on state); else `OK`. Without the flag it answers `UNKNOWN` and sends nothing. It changes nothing, so it is not refused during a FLUSH: it waits for the transfer in flight (`esp_lcd`'s `rx_param` does, a few ms) and the app can read it while the screen draws. The adapter reads it every `CONFIG_SYS_LVGL_HEALTH_MS` while it draws (SYS_LVGL.MD "The display's health").

### Controllers (`sys_display_panel_e`)

`panel` names the controller, whose init sequence the driver sends: ILI9341 (0), ST7789 (1), ST7735 (2), GC9A01 (3), ST7796 (4), ILI9488 (5). A driver refuses one it has no sequence for (`ERR_DEV_RANGE`).

### Pixel formats (`sys_display_format_e`)

The format says what the bytes of a buffer are, exactly as the driver sends them; the adapter picks the LVGL format from it and the driver converts nothing.

| Format | Bytes per pixel | LVGL | Panels |
|---|---|---|---|
| `RGB565_SWAPPED` (0) | 2, the high byte first | `LV_COLOR_FORMAT_RGB565_SWAPPED` (`LV_DRAW_SW_SUPPORT_RGB565_SWAPPED`) | ST7789, ILI9341, ST7735, GC9A01, ST7796 |
| `RGB888` (1) | 3: B, G, R; an 18-bit panel takes the 6 high bits | `LV_COLOR_FORMAT_RGB888` | ILI9488 (18 bit only on SPI); the driver sets the colour order so that B, G, R come out right |

## The end of a FLUSH

The contract has no events. The driver calls the FLUSH's **`done(done_ctx)`** once, from the transfer-done interrupt (with `esp_lcd`: `on_color_trans_done`), after every **accepted** FLUSH; never after a FLUSH the driver or the registry refused (`BAD_ARG`, `BAD_WINDOW`, `FLUSH_BUSY`, `TRANSFER`, `SUSPENDED`): those come back from FLUSH as an `err_h`, and the adapter releases LVGL itself. `done` carries no error and runs in an ISR: it gives a semaphore (`xSemaphoreGiveFromISR`) and nothing else; it takes no lock and calls no device. Once the display's RESET returns, no `done` of a FLUSH accepted before it comes: RESET stops the transfer in flight (or waits for its end) before it returns.

Why not an event: the event queue has one task. The LVGL task holds the LVGL lock while it waits for the end of a FLUSH; an event listener that took the LVGL lock (a screen updated on a device's event) would hold the event task, and the end of the FLUSH queued behind it would never come: a deadlock. The callback ends the FLUSH without any task in between.

## Transfer errors

SPI has no acknowledge: once a transfer runs, the bus cannot tell whether the panel took it, and its done callback has no status. A transfer goes wrong in two places:

| When | What | Who reports it, how |
|---|---|---|
| In FLUSH, before the DMA starts | the bus refuses it: `tx_color` / `spi_device_queue_trans` fails (an argument, no memory for a bounce buffer, the bus not acquired) | the driver, **at once**: FLUSH returns `ERR_DISPLAY_TRANSFER{id, code}` (`code` the `esp_err_t`), `done` is not called |
| The transfer never ends | the DMA or the bus hangs: no interrupt, no `done` | the adapter: its `flush_wait_cb` waits at most a limit (several times one buffer's transfer), then reports `ERR_DISPLAY_FLUSH_TIMEOUT{id, timeout_ms}`, releases LVGL and RESETs the display (the driver stops the bus's transfer and inits the panel again) |

Bytes that reach the panel wrong (noise on a long wire, a loose contact) are not seen by anyone: `done` comes and the picture is wrong until that area is drawn again.

A FLUSH that fails while some of its chunks are queued (`esp_lcd` enables the done callback on the last chunk only) has no `done`: the driver returns the error, and recycles the finished transactions before its next transfer (`esp_lcd` does it in the next `tx_param`).

## Create

`display_cfg_t`, every difference of the glass in the config (the same firmware on every board):

| Group | Fields | Notes |
|---|---|---|
| Bus | `bus` (sys_spi), `spi_mode` (0..3), `freq_hz` | a module without CS often needs mode 3 |
| Pins | `cs`, `dc`, `reset`, `backlight` (`sys_io_pin_t`) | `cs` and `dc` must be pins of the ESP32-S3 GPIO device (switched in every transaction), else `ERR_DEV_BAD_PIN`; `dc` is required; the rest `SYS_IO_PIN_NONE` when unused |
| Backlight | `brightness_default` | |
| Glass | `width`, `height` (native, orientation 0), `offset_x`, `offset_y` (where the glass starts in the controller's RAM, its address order) | |
| Controller | `panel` (the driver's init sequence), `orientation` (at start) | |
| Flags | `flags`: `SYS_DISPLAY_FLAG_BGR` (the glass's subpixels blue, green, red), `_INVERT` (most IPS glass), `_MIRROR_X`, `_MIRROR_Y` (the glass mounted mirrored), `_BL_ACTIVE_LOW` (the backlight on when its pin is low), `_READABLE` (the module's SDO is wired: HEALTH reads the panel) | 0: an RGB glass, not inverted, not mirrored, backlight active high, not read. A mirrored glass would show the picture mirrored at orientation 0: the driver adds the mirror to its scan direction (MADCTL MX / MY) in every orientation, and the offsets stay in the RAM's own order. A bit past `SYS_DISPLAY_FLAGS_ALL` is `ERR_DEV_RANGE` |

## What the LVGL adapter must do (`sys_lvgl`)

The adapter is SYS_LVGL.MD (`components/sys/sys_lvgl`). What it does with this contract:

- Draw buffers `MALLOC_CAP_DMA` in internal RAM (a buffer elsewhere is copied by the SPI driver in every transaction), 4-byte aligned, each at most `flush_max`.
- LVGL in PARTIAL mode, `LV_DRAW_BUF_STRIDE_ALIGN` 1 (rows without padding: `w × h × bpp` contiguous bytes).
- `flush_cb` calls FLUSH with `px_map` (the start of buffer 1 or 2), `len` = the area's bytes and the area. A FLUSH that fails, whatever the error, releases LVGL at once (`lv_display_flush_ready`), reports the error (`err_report`) and has the screen invalidated after the refresh: LVGL asserts on an invalidation during rendering (`lv_refr.c`, "Invalidate area is not allowed during rendering"), and `flush_cb` runs inside it.
- A `flush_wait_cb` that waits on the semaphore the FLUSH's `done` gives, at most the FLUSH limit ("Transfer errors"); LVGL clears its `flushing` flag when it returns. Without it LVGL spins in `while(disp->flushing);` holding its lock. `done_ctx` carries the FLUSH's number, so a `done` of a FLUSH given up on is ignored.
- The `lv_display` is created in the panel's native size (GEOMETRY's width and height swapped at 90 and 270) and its rotation always equals the display's ORIENTATION: at INSTALL (the config's `orientation` may be other than 0), after ROTATION and after the display's RESET. LVGL turns the touch point by that rotation (SYS_TOUCH.MD).
- STOP and RESET wait for the last `done` (the semaphore), never for an event.
- The display changing under it: after a RESET of its display (the orientation and brightness are the config's again, the panel's content is gone) and after RESUME, it sets its rotation again and invalidates the screen; while the display is suspended (FLUSH answers `ERR_DEV_SUSPENDED`) it pauses rendering. runit's device state listener tells it (`sys_lvgl_device_state`), which only sets a notification bit of the LVGL task.
- The display is not removed while the adapter is installed: a project change RESETs the adapter before it removes the devices. (The registry protects a device from REMOVE only against another device that uses it, `ERR_DEV_IN_USE`.)
- Locks: the LVGL task holds the LVGL lock across FLUSH (LVGL lock, then the registry's). So the LVGL lock is **never** taken under the registry lock (inside a device op, the state listener or an inline event listener): that order is a deadlock. A listener of the event task **should not** take it either: it would not deadlock (the end of a FLUSH comes from the ISR), but the event task would wait up to a whole frame and hold every other event back. `lv_async_call` needs the lock too (it allocates and makes a timer), so it is no way to post from another task.

## Errors (`ERR_MOD_SYS_DISPLAY`)

| Tag | Meaning |
|---|---|
| `ERR_DISPLAY_BAD_WINDOW` | The window is empty, outside the panel, its bytes are not the FLUSH's `len`, or `len` is above `flush_max` |
| `ERR_DISPLAY_FLUSH_BUSY` | A FLUSH is still in flight |
| `ERR_DISPLAY_TRANSFER` | The bus refused the transfer (from FLUSH at once); `code` the bus's `esp_err_t` |
| `ERR_DISPLAY_FLUSH_TIMEOUT` | The end of an accepted FLUSH did not come within `timeout_ms`: the adapter reports it and resets the display |
| `ERR_DISPLAY_LOST` | HEALTH: the panel does not answer (`health` NO_ANSWER) or lost its power (RESTARTED: the adapter resets it); `power_mode` what it answered. The adapter reports it once until the panel answers again |

## Open

- `sys_spi`: the firmware has no SPI bus yet; a display driver needs it (MOSI, SCK, MISO with its pull-up for HEALTH, `max_transfer_sz`).
- No display driver on the board yet. The PC simulator has one (`lvgl-playground/driver/lcd_spi.c`: `esp_lcd` panel IO, ILI9341) on a simulated SPI bus and controller; it moves to `devices/` with `sys_spi`.
