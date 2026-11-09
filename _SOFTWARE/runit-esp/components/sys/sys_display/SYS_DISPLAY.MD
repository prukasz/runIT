# sys_display — the display contract

Not a module with state of its own: a set of device ops that mean the same on every display driver, like the power contracts (SYS_POWER.MD), nothing else. The wrappers in `sys_display.c` only build the `sys_device_call`. The adapter that draws on a display (`sys_lvgl`) knows this contract, not the chip.

A display is a colour panel on **SPI**: ST7789, ILI9341, ST7735, GC9A01, ST7796, ILI9488 (1.3" to 3.5", up to 480×320). No I2C panels (a colour frame at 400 kHz takes seconds); touch is a separate device on its own contract and bus.

| File | Contents |
|---|---|
| `sys_display_contract.h` | DISPLAY: contract table, event, enums, `display_cfg_t`, `display_geometry_t`, `display_window_t`, the op args, wrappers |
| `contract_display.json` | The contract's descriptor: GEOMETRY, BACKLIGHT, ORIENTATION (FLUSH is local, not a command) |
| `errors/sys_display_errors.h`, `errors/errors_sys_display.json` | `ERR_SYS_DISPLAY_MAP`, the module's error tags, their messages |
| `sys_display.c` | The error module and the wrappers. `WHOLE_ARCHIVE`: a driver raises `ERR_DISPLAY_*` without calling a wrapper |
| `CMakeLists.txt` | Component |
| `AGENTS.md` | Rules for changing this folder |

## DISPLAY (`0x0Axx`)

| Op | Side | Wrapper | Args | Answer |
|---|---|---|---|---|
| `0x0A00` GEOMETRY | R | `sys_display_get_geometry(dev, &g)` | – | `display_geometry_t` (width, height in the current orientation, format) |
| `0x0A01` BACKLIGHT | W | `sys_display_set_backlight(dev, duty)` | `{u16 brightness_duty}` 0 .. 4095 | – |
| `0x0A02` FLUSH | W, **local** | `sys_display_flush(dev, pixels, len, window, done, ctx)` | `display_flush_args_t {const void* pixels, u32 len, display_window_t window, done, done_ctx}` (C only) | – |
| `0x0A03` ORIENTATION | RW | `sys_display_set_orientation(dev, o)`, `sys_display_get_orientation(dev, &o)` | `{u8 orientation}` (`sys_display_orientation_e`) | the same |

FLUSH carries pointers (the adapter's pixels, its callback), so it is **local** (`SYS_DEVICE_OP_LOCAL` in the contract table, SYS_DEVICE.MD): the device router and a remote device refuse it with `ERR_DEV_LOCAL_OP`, so its args never come from a frame. Only the adapter on this board calls it, one at a time. No VM block may have FLUSH: the VM calls `sys_device_call` past the router.

- **FLUSH**: the driver checks `pixels` (not NULL, DMA-capable memory: `esp_ptr_dma_capable`, else `ERR_DEV_BAD_ARG`) and the window (inside the panel, and `w × h × bytes per pixel` of the format equal to `len`, else `ERR_DISPLAY_BAD_WINDOW`), sets the window and **queues** the pixels (CASET, RASET, RAMWR) and returns. It does not wait for the transfer: FLUSH runs under the registry lock (SYS_DEVICE.MD "Concurrency"). One FLUSH in flight (`ERR_DISPLAY_FLUSH_BUSY`). The adapter leaves the pixels untouched until `done`.
- **GEOMETRY** is read by the adapter at its INSTALL, to size the draw buffers (the driver does not know them), and after every ORIENTATION. The remote side reads GEOMETRY and sets BACKLIGHT and ORIENTATION.
- **ORIENTATION** turns the picture in the panel itself (the controller's scan direction: MADCTL). The driver works out the RAM offset for the new orientation from the native `offset_x`, `offset_y`, and GEOMETRY answers the new width and height. LVGL draws in PARTIAL mode, where it cannot turn the picture itself without a third buffer; the adapter's ROTATION is this op, then `lv_display_set_rotation`.
- **BACKLIGHT** goes to the IO PWM of the device that has `backlight` (`IO_PWM_DUTY`, 12 bits), inverted when `bl_active_level` is 0. CREATE and RESUME set `brightness_default`; SUSPEND turns it off and sleeps the panel.

### Pixel formats (`sys_display_format_e`)

The format says what the bytes of a buffer are, exactly as the driver sends them; the adapter picks the LVGL format from it and the driver converts nothing.

| Format | Bytes per pixel | LVGL | Panels |
|---|---|---|---|
| `RGB565_SWAPPED` (0) | 2, the high byte first | `LV_COLOR_FORMAT_RGB565_SWAPPED` (`LV_DRAW_SW_SUPPORT_RGB565_SWAPPED`) | ST7789, ILI9341, ST7735, GC9A01, ST7796 |
| `RGB888` (1) | 3: B, G, R; an 18-bit panel takes the 6 high bits | `LV_COLOR_FORMAT_RGB888` | ILI9488 (18 bit only on SPI); the driver sets the colour order so that B, G, R come out right |

## The end of a FLUSH

The contract has no events. The driver calls the FLUSH's **`done(done_ctx)`** once, from the transfer-done interrupt (with `esp_lcd`: `on_color_trans_done`), after every **accepted** FLUSH, also after a failed transfer; never after a FLUSH the driver or the registry refused (`BAD_ARG`, `BAD_WINDOW`, `FLUSH_BUSY`, `TRANSFER`, `SUSPENDED`): those come back from FLUSH as an `err_h`, and the adapter releases LVGL itself. `done` carries no error and runs in an ISR: it gives a semaphore (`xSemaphoreGiveFromISR`) and nothing else; it takes no lock and calls no device.

Why not an event: the event queue has one task. The LVGL task holds the LVGL lock while it waits for the end of a FLUSH; an event listener that took the LVGL lock (a screen updated on a device's event) would hold the event task, and the end of the FLUSH queued behind it would never come: a deadlock. The callback ends the FLUSH without any task in between.

## Transfer errors

SPI has no acknowledge: once a transfer runs, the bus cannot tell whether the panel took it, and its done callback has no status. A transfer goes wrong in three places:

| When | What | Who reports it, how |
|---|---|---|
| In FLUSH, before the DMA starts | the bus refuses it: `tx_color` / `spi_device_queue_trans` fails (an argument, no memory for a bounce buffer, the bus not acquired) | the driver, **at once**: FLUSH returns `ERR_DISPLAY_TRANSFER{id, code}` (`code` the `esp_err_t`), `done` is not called |
| After it was accepted, known in the ISR | (not on SPI with `esp_lcd`; a driver that chains its own transfers in the ISR) | the driver, **at the next FLUSH**: the ISR keeps only the code and still calls `done`; the next FLUSH returns `ERR_DISPLAY_TRANSFER{id, code}` once and sends nothing. RESET and RESUME clear the kept code |
| The transfer never ends | the DMA or the bus hangs: no interrupt, no `done` | the adapter: its `flush_wait_cb` waits at most a limit (several times one buffer's transfer), then reports `ERR_DISPLAY_FLUSH_TIMEOUT{id, ms}`, releases LVGL and RESETs the display (the driver stops the bus's transfer and inits the panel again) |

A FLUSH that fails while some of its chunks are queued (`esp_lcd` enables the done callback on the last chunk only) has no `done`: the driver returns the error, and recycles the finished transactions before its next transfer (`esp_lcd` does it in the next `tx_param`).

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
- `flush_cb` calls FLUSH with `px_map` (the start of buffer 1 or 2), `len` = the area's bytes and the area. A FLUSH that fails, whatever the error, releases LVGL at once (`lv_display_flush_ready`), invalidates the screen (the window that failed is drawn again in the next frame) and reports the error (`err_report`).
- A `flush_wait_cb` that waits on the semaphore the FLUSH's `done` gives, at most the FLUSH limit ("Transfer errors"), then `lv_display_flush_ready`. Without it LVGL spins in `while(disp->flushing);` holding its lock.
- STOP and RESET wait for the last `done` (the semaphore), never for an event.
- Locks: the LVGL task holds the LVGL lock across FLUSH (LVGL lock, then the registry's). So the LVGL lock is never taken under the registry lock (inside a device op or an inline event listener), and a listener of the event task does not take it either: a screen change on an event goes to the LVGL task (`lv_async_call`).

## Errors (`ERR_MOD_SYS_DISPLAY`)

| Tag | Meaning |
|---|---|
| `ERR_DISPLAY_BAD_WINDOW` | The window is empty, outside the panel, or its bytes are not the FLUSH's `len` |
| `ERR_DISPLAY_FLUSH_BUSY` | A FLUSH is still in flight |
| `ERR_DISPLAY_TRANSFER` | The bus refused the transfer (from FLUSH at once) or one accepted before failed (from the next FLUSH, once); `code` the bus's `esp_err_t` |
| `ERR_DISPLAY_FLUSH_TIMEOUT` | The end of an accepted FLUSH did not come within `ms`: the adapter reports it and resets the display |

## Open

- `sys_spi`: the firmware has no SPI bus yet; a display driver needs it.
- No display driver yet, and no `sys_lvgl`.
