# sys_lvgl

Spec: `SYS_LVGL.MD` (this folder).

- Every change in this folder is followed by an edit of `SYS_LVGL.MD` and this file, if they are made stale.
- The op numbers, `sys_lvgl_install_t`, `sys_lvgl_rotation_t`, `sys_lvgl_status_t`, `sys_lvgl_state_e` and the error tags are wire format: never renumber, new ones at the end; a change edits `router/contract_lvgl.json` or `errors/errors_sys_lvgl.json` and runs `sh templates/sync.sh`.
- The adapter knows the contracts DISPLAY and TOUCH only (`sys_display_*`, `sys_touch_*`, `sys_device_read` / `_write` of the lifecycle ops), never a chip, a bus or a pin.
- Everything LVGL allocates is in its one pool (`lv_conf.h`, `sys_lvgl_pool.h`): never point LVGL at malloc, never keep LVGL memory past RESET (`lv_deinit` frees the pool). LVGL must never reach the end of the pool (it crashes): code that makes widgets calls `sys_lvgl_pool_check` first; the asserts stay on.
- Every `lv_*` call is under the LVGL lock (`sys_lvgl_lock`; LVGL has `LV_OS_NONE`), and only while installed. Never take it under the registry lock; code that runs there (`sys_lvgl_device_state`, a device op) only sets a bit and notifies the task. No `lv_async_call` from another task.
- The `done` callback runs in an ISR: it reads `s_flight` and gives the semaphore, nothing else.
- `lv_conf.h` is the one LVGL configuration, for the board and `lvgl-playground/`; the sdkconfig's LVGL menu is ignored (`LV_KCONFIG_IGNORE`). A setting goes here, not into `sdkconfig`.
- Platform code (`heap_caps_*`) stays behind `#ifdef ESP_PLATFORM`; everything else builds on `runit-esp/test_host`'s stand-ins, so the simulator keeps building: after a change, run `lvgl-playground/run.ps1 -Headless -Scenario scenarios\walkthrough.txt`.
- Keep `WHOLE_ARCHIVE` in `CMakeLists.txt`: the error tags are registered by a constructor.
