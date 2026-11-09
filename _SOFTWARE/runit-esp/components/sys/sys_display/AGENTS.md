# sys_display

Spec: `SYS_DISPLAY.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- The op numbers (0 GEOMETRY, 1 BACKLIGHT, 2 FLUSH) and their sides, the event number (0 FLUSH_DONE), the enums (`sys_display_format_e`, `sys_display_orientation_e`), the packed layouts (`display_cfg_t`, `display_geometry_t`, `display_window_t`, `display_backlight_args_t`, `display_flush_args_t` 9 B) and the error tags are wire format: do not renumber or re-layout them. New ones go at the end.
- Changing the contract table, an enum, a layout or an error requires an update in `templates/`: run `sh templates/sync.sh`. The contract descriptor and the error messages follow the tables.
- No pointer in a frame of the wire, ever: `FLUSH` names its pixels by a buffer id (`display_flush_args_t.buffer_id`), and the driver resolves it with `sys_display_buffer_get`. The buffer table is C only (not an op); only the adapter sets it (`sys_display_buffer_set`).
- A buffer is not cleared or freed while a FLUSH of it is in flight. The driver checks the window against the panel and the buffer size before it sends.
- The contract ID (`SYS_DEVICE_CONTRACT_DISPLAY`) is in `sys_device.h`, the module ID (`ERR_MOD_SYS_DISPLAY`) in `err_modules.h`, not here.
- A driver does not know the draw buffers and always publishes `FLUSH_DONE`, also after a failed transfer. One `FLUSH` in flight (`ERR_DISPLAY_FLUSH_BUSY`).
- `SYS_DISPLAY_BRIGHTNESS_MAX` is `IO_PWM_DUTY_MAX` (4095).
- The op wrappers only build `sys_device_call`. No driver logic goes here.
- Keep `WHOLE_ARCHIVE` in `CMakeLists.txt`: the error tags are registered by a constructor.
