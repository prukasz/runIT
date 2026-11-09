# sys_display — the display contract

Not a module with state of its own: a set of device ops that mean the same on every display driver, like the power contracts (SYS_POWER.MD), plus the table of draw buffers a FLUSH names. The wrappers in `sys_display.c` only build the `sys_device_call`. The adapter that draws on a display (`sys_lvgl`) knows this contract, not the chip.

| File | Contents |
|---|---|
| `sys_display_contract.h` | DISPLAY: contract table, event, enums, `display_cfg_t`, `display_geometry_t`, `display_window_t`, wrappers, the buffer table |
| `errors/sys_display_errors.h` | `ERR_SYS_DISPLAY_MAP`, the module's error tags |
| `sys_display.c` | The error module, the wrappers and the buffer table. `WHOLE_ARCHIVE`: a driver raises `ERR_DISPLAY_*` without calling a wrapper |
| `CMakeLists.txt` | Component |
| `AGENTS.md` | Rules for changing this folder |

## DISPLAY (`0x0Axx`)

| Op | Side | Wrapper | Args | Answer |
|---|---|---|---|---|
| `0x0A00` GEOMETRY | R | `sys_display_get_geometry(dev, &g)` | – | `display_geometry_t` (width, height, format) |
| `0x0A01` BACKLIGHT | W | `sys_display_set_backlight(dev, duty)` | `{u16 brightness_duty}` 0 .. 4095 | – |
| `0x0A02` FLUSH | W | `sys_display_flush(dev, buffer, window)` | `{u8 buffer_id, display_window_t window}` | – |

No frame holds a pointer: FLUSH names its pixels by a buffer id, so every op is safe on the wire.

- **FLUSH**: the driver reads the buffer with `sys_display_buffer_get(buffer, &pixels, &size)`, checks that the window fits the panel and the buffer (`ERR_DISPLAY_BAD_WINDOW`), sets the window, sends the pixels in chunks and returns when it has accepted them. A buffer id not set or past `SYS_DISPLAY_BUFFERS`: `ERR_DISPLAY_BAD_BUFFER`. The buffer stays set until `FLUSH_DONE`. One FLUSH in flight.
- **GEOMETRY** is read by the adapter at its INSTALL, to size the draw buffers; the driver does not know them. The remote side reads GEOMETRY and sets BACKLIGHT.

| Event | ID | Flags | Value |
|---|---|---|---|
| `FLUSH_DONE` | `0x0A00` | – | 0, or the error code of the transfer (the `.code` of `ERR_DISPLAY_TRANSFER`) |

Published after the last chunk (from the DMA interrupt, deferred to the event task), also after a failed transfer. No NOTIFY: it is for the adapter, not the app. FLUSH is asynchronous: it returns once the driver has accepted the pixels, and `FLUSH_DONE` ends it. The value is a code, not an `err_h`: the event holds no pointer, an `err_h` has one owner and the event has any number of listeners. The adapter, in the event task, builds the error from it: `err_report(ERR_NEW(ERR_DISPLAY_TRANSFER, .id = ev->source, .code = ev->value))`. The errors of the call itself (`BAD_BUFFER`, `BAD_WINDOW`, `FLUSH_BUSY`, `SUSPENDED`) come back from FLUSH as an `err_h`.

## Buffer table

`SYS_DISPLAY_BUFFERS` (2: double buffering) entries `{pixels, size}`, global to the module. The adapter allocates its draw buffers once at INSTALL and sets them with `sys_display_buffer_set(id, pixels, size)`; it clears them (`NULL, 0`) at RESET, never while a FLUSH of the buffer is in flight. The driver reads one with `sys_display_buffer_get` (C only: the table is not an op). A peer that sends a FLUSH over the wire can name only a buffer the adapter set, so the worst it does is draw it.

## Create

`display_cfg_t`: bus, I2C address, pins (cs, dc, reset, backlight; `SYS_IO_PIN_NONE` when unused), the panel's native size (width, height; GEOMETRY answers it in the current orientation), panel variant, orientation.

## Errors (`ERR_MOD_SYS_DISPLAY`)

| Tag | Meaning |
|---|---|
| `ERR_DISPLAY_BAD_WINDOW` | The window is empty, outside the panel or past the buffer |
| `ERR_DISPLAY_FLUSH_BUSY` | A FLUSH is still in flight |
| `ERR_DISPLAY_TRANSFER` | The bus transfer failed |
| `ERR_DISPLAY_BAD_BUFFER` | The buffer id is past `SYS_DISPLAY_BUFFERS`, or the buffer is not set |
