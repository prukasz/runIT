# sys_display

Spec: `SYS_DISPLAY.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- SPI colour panels only. No I2C display, no mono format.
- The op numbers (0 GEOMETRY, 1 BACKLIGHT, 2 FLUSH, 3 ORIENTATION) and their sides, the enums (`sys_display_format_e`, `sys_display_orientation_e`, `sys_display_rgb_order_e`), the packed layouts (`display_cfg_t`, `display_geometry_t`, `display_window_t`, `display_backlight_args_t`, `display_orientation_args_t`) and the error tags are wire format: do not renumber them. New ones go at the end.
- Changing the contract table, an enum, a layout or an error requires an edit of `contract_display.json` or `errors/errors_sys_display.json` and `sh templates/sync.sh`. FLUSH and ORIENTATION are not in the descriptor: they are not commands.
- ORIENTATION is local too (`SYS_DEVICE_OP_LOCAL`): only the screen adapter turns the panel, so LVGL always knows its size. A driver refuses it while a FLUSH is in flight (`ERR_DISPLAY_FLUSH_BUSY`).
- `display_flush_args_t` carries pointers (the pixels, the `done` callback; C only, not packed), so FLUSH is local: `SYS_DEVICE_OP_LOCAL` in `DISPLAY_OPERATIONS` (every driver's `OP_ENTRY_W(DISPLAY, FLUSH)` gets it; never drop the flag), and no VM block or stored action may have FLUSH. This is what keeps a frame from handing a driver a pointer.
- The pixels of a FLUSH are not changed or freed until its `done`. The driver checks the pointer (not NULL, DMA-capable) and the window against the panel and `len` before it sends.
- A driver's FLUSH only queues the transfer and returns (it runs under the registry lock). One `FLUSH` in flight (`ERR_DISPLAY_FLUSH_BUSY`).
- The contract has no events. A driver calls the FLUSH's `done` once after every accepted FLUSH, also after a failed transfer, never after a refused one, from the transfer-done interrupt. `done` carries no error and does only what an ISR may (give a semaphore).
- Transfer errors (SYS_DISPLAY.MD "Transfer errors"): the bus refusing it is returned from FLUSH at once; a failure known only in the ISR is kept as a code and returned once by the next FLUSH (which sends nothing); RESET and RESUME clear it. A hang is the adapter's `ERR_DISPLAY_FLUSH_TIMEOUT`, then RESET of the display, which must stop a running transfer.
- The LVGL lock is never taken under the registry lock (a deadlock), and a listener of the event task does not take it (it would hold every event back for up to a frame): it posts to the LVGL task (SYS_DISPLAY.MD "What the LVGL adapter must do").
- The format a driver answers in GEOMETRY is the byte layout it sends unchanged: it converts no pixels.
- Every difference of the glass (offsets, colour order, inversion, clock, SPI mode, backlight level) is a field of `display_cfg_t`, not an entry of the driver's `panel` list (that one is only the controller's init sequence).
- The contract ID (`SYS_DEVICE_CONTRACT_DISPLAY`) is in `sys_device.h`, the module ID (`ERR_MOD_SYS_DISPLAY`) in `err_modules.h`, not here.
- `SYS_DISPLAY_BRIGHTNESS_MAX` is `IO_PWM_DUTY_MAX` (4095).
- The op wrappers only build `sys_device_call`. No driver logic goes here.
- Keep `WHOLE_ARCHIVE` in `CMakeLists.txt`: the error tags are registered by a constructor.
