# sys_touch

Spec: `SYS_TOUCH.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- One point. No multi-touch, pressure or gestures in the contract: a chip's own ops (`0xFFxx`) have them.
- The op numbers (0 POINT, 1 CALIBRATION) and their sides, the axes flags (`TOUCH_AXES_*`), the packed layouts (`touch_cfg_t`, `touch_point_t`, `touch_calibration_t`) and the error tags are wire format: do not renumber them. New ones go at the end.
- Changing the contract table, a flag, a layout or an error requires an edit of `contract_touch.json` or `errors/errors_sys_touch.json` and `sh templates/sync.sh`.
- POINT answers in the display's pixels, in its orientation 0, always: a driver never follows the display's ORIENTATION (LVGL turns the point). `axes` is how the touch panel lies on the glass.
- A sleeping chip that does not acknowledge, and a suspended device, answer not pressed: no error, no bus traffic while suspended.
- The interrupt line's meaning (level or pulse) stays in the driver; POINT means the same with or without it.
- CALIBRATION is for resistive panels; a capacitive driver leaves it out with `has_op`. Its write lasts until the next CREATE; the config holds the lasting value.
- The contract ID (`SYS_DEVICE_CONTRACT_TOUCH`) is in `sys_device.h`, the module ID (`ERR_MOD_SYS_TOUCH`) in `err_modules.h`, not here.
- The op wrappers only build `sys_device_call`. No driver logic goes here.
- Keep `WHOLE_ARCHIVE` in `CMakeLists.txt`: the error tags are registered by a constructor.
