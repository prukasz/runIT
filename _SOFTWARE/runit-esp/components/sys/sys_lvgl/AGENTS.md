# sys_lvgl

Spec: `SYS_LVGL.MD` (this folder).

- Every change in this folder is followed by an edit of `SYS_LVGL.MD` and this file, if they are made stale.
- The op numbers, `sys_lvgl_install_t`, `sys_lvgl_rotation_t`, `sys_lvgl_status_t`, `sys_lvgl_state_e` and the error tags are wire format: never renumber, new ones at the end; a change edits `router/contract_lvgl.json` (`LVGL_OPERATIONS` in `sys_lvgl_router.h`) or `errors/errors_sys_lvgl.json` and runs `sh templates/sync.sh`.
- The adapter knows the contracts DISPLAY and TOUCH only (`sys_display_*`, `sys_touch_*`, `sys_device_read` / `_write` of the lifecycle ops), never a chip, a bus or a pin.
- Everything LVGL allocates is in its one pool (`config/lv_conf.h`, `config/sys_lvgl_hooks.h`): never point LVGL at malloc, never keep LVGL memory past RESET (`lv_deinit` frees the pool). LVGL must never reach the end of the pool (it crashes): code that makes widgets calls `sys_lvgl_pool_check` first; the asserts stay on.
- Every `lv_*` call is under the LVGL lock (`sys_lvgl_lock`; LVGL has `LV_OS_NONE`), and only while installed. Never take it under the registry lock; a device op and the event listener only set a bit and notify the task. No `lv_async_call` from another task.
- The events (`LVGL_EVENTS`: codes, payloads) are wire format: never renumber, new ones at the end; a change edits the `events` of `router/contract_lvgl.json`. `LVGL_EV_RESET` is published inline under the LVGL lock and its listener drops what it made in LVGL and returns; `STATE` and `ROTATION` are always deferred.
- The `done` callback runs in an ISR: it reads `s_flight` and gives the semaphore, nothing else.
- `config/` is the one LVGL configuration, for the board and `lvgl-playground/`; the sdkconfig's LVGL menu is ignored (`LV_KCONFIG_IGNORE`). A setting goes into the section of its subject in `lv_conf.h` (drawing, memory, system, fonts) or into `lv_conf_widgets.h`, never into `sdkconfig`. `lv_conf_widgets.h` lists every widget on or off; a widget turned on or off there changes what the screen layer may offer, and its doc follows.
- The adapter does not hold its display and touch: a REMOVED event of either resets it (`BIT_GONE`). The registry's event listener (`on_device_event`) touches only atomics, the semaphore and the task's bits.
- Platform code (`heap_caps_*`) stays behind `#ifdef ESP_PLATFORM`; everything else builds on `runit-esp-archive/test_host`'s stand-ins, so the simulator keeps building: after a change, run `lvgl-playground/run.ps1 -Headless -Scenario scenarios\walkthrough.txt`.
- Keep `WHOLE_ARCHIVE` in `CMakeLists.txt`: the error tags are registered by a constructor.
