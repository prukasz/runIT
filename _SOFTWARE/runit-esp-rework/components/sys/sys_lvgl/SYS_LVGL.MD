# sys_lvgl — the LVGL adapter

LVGL on one display (contract DISPLAY, SYS_DISPLAY.MD) and an optional touch (contract TOUCH, SYS_TOUCH.MD): LVGL's task and lock, the draw buffers, LVGL's one pool, the rotation, drawing on or off. It knows the two contracts, never a chip or a bus. It is not a device: its commands are under its own router key. What is on the screen is the upper layers' (`sys_screen`, not written yet); they touch `lv_*` only under `sys_lvgl_lock()`.

| File | Contents |
|---|---|
| `sys_lvgl.h` | The C API, `sys_lvgl_install_t`, `sys_lvgl_rotation_t`, `sys_lvgl_status_t`, `sys_lvgl_state_e`, the events (`LVGL_EVENTS`) |
| `sys_lvgl.c` | The task, INSTALL / RESET, FLUSH and its end, the touch read, the error module |
| `config/` | LVGL's configuration, the same on the board and in the PC simulator ("LVGL configuration"): `lv_conf.h` (drawing, memory, system, fonts), `lv_conf_widgets.h` (the widgets), `sys_lvgl_hooks.h` (LVGL's pool hook and assert handler) |
| `router/sys_lvgl_router.{h,c}`, `router/contract_lvgl.json` | The commands under `MODULE_ID_LVGL` (0x29, `LVGL_OPERATIONS`) and the descriptor of the commands and the events |
| `errors/sys_lvgl_errors.h`, `errors/errors_sys_lvgl.json` | `ERR_MOD_SYS_LVGL` (= `MODULE_ID_LVGL`), its tags and messages |
| `idf_component.yml` | LVGL 9.3.0 from the component manager (`managed_components/`, not committed) |
| `Kconfig`, `CMakeLists.txt` | Settings; the component, LVGL pointed at `config/lv_conf.h` |

## Commands (`MODULE_ID_LVGL` 0x29)

Frames `[0x29][dir][op][args]`, packed, little-endian. Each is a command like any other, so a stored project replays them (INSTALL after its devices).

| Op | Side | Args / answer |
|---|---|---|
| `0x01` INSTALL | RW | W: `sys_lvgl_install_t {u8 display, u8 touch (0xFF none), u32 heap_bytes, u8 refresh_hz}` (7 B): `heap_bytes` LVGL's pool, 32 KB .. 4 MB, 0 = `CONFIG_SYS_LVGL_HEAP_DEFAULT`; `refresh_hz` frames a second at most and the touch read as often, 1 .. 60, 0 = `SYS_LVGL_REFRESH_HZ` (24, `lv_conf.h`); R: the same, `0xFF` and 0 when empty |
| `0x02` RESET | W | – |
| `0x03` ROTATION | RW | `{u8 rotation}`, `sys_display_orientation_e` (0, 90, 180, 270 deg) |
| `0x04` STATUS | R | `sys_lvgl_status_t {u8 state, u8 rotation, u8 buffers, u16 width, u16 height, u32 buffer_bytes, u32 heap_budget, u32 heap_used, u32 heap_peak, u32 flushes, u32 flush_errors}` (31 B); width and height with the rotation |
| `0x05` START | W | – |
| `0x06` STOP | W | – |

## States

| State | What | Into it |
|---|---|---|
| EMPTY | LVGL not running (no `lv_init`), no memory held, nothing drawn | boot, RESET, a failed INSTALL |
| RUNNING | draws, reads the touch | INSTALL, START, the display resumed |
| STOPPED | neither draws nor reads the touch; the screen, its objects and the buffers stay; edits under the lock still work and show at START | STOP |
| PAUSED | the display is suspended (its writes answer `ERR_DEV_SUSPENDED`): as STOPPED until it resumes | the display's SUSPEND (its own, a sweep, the safe state) |

STOP and PAUSED are separate: a display that resumes while STOPPED stays stopped.

## INSTALL

Under the LVGL lock, in order; a failing step undoes what was done and leaves the adapter EMPTY:

1. The display's GEOMETRY and ORIENTATION, wrapped in `ERR_LVGL_NOT_DISPLAY{id}` when they fail (not a display, a remote one: ORIENTATION is local). The format gives LVGL's: `RGB565_SWAPPED` → `LV_COLOR_FORMAT_RGB565_SWAPPED`, `RGB888` → `LV_COLOR_FORMAT_RGB888`, else `ERR_LVGL_FORMAT`.
2. The touch, if any: one POINT, wrapped in `ERR_LVGL_NOT_TOUCH{id}` when it fails. Its `max_x`, `max_y` are not checked (no op reads a config): the app's configurator fills them from the display.
3. (The display and the touch are not held: a REMOVE of either while installed resets the adapter, "The display's state".)
4. Two draw buffers (one when the second does not fit): a tenth of the screen in whole rows of its longer side, at least one row, at most GEOMETRY's `flush_max` and `CONFIG_SYS_LVGL_BUFFER_MAX` (`ERR_LVGL_FLUSH_MAX` when one row is more than `flush_max`). `MALLOC_CAP_DMA` internal RAM, 4-byte aligned (`ERR_LVGL_NO_MEMORY`).
5. LVGL's pool: one block of `heap_bytes` in internal RAM (`ERR_LVGL_HEAP` outside 32 KB .. 4 MB, `ERR_LVGL_NO_MEMORY`), `lv_init` on it ("The pool").
6. The refresh: `refresh_hz` (`ERR_LVGL_REFRESH` outside 1 .. 60), the period of LVGL's refresh timer and of the touch's read timer.
7. The `lv_display` in the panel's **native** size (GEOMETRY's width and height swapped at 90 and 270), PARTIAL, `flush_cb`, `flush_wait_cb`, its rotation = ORIENTATION. LVGL turns the touch point by that rotation, so the touch answers in the panel's orientation 0 (SYS_TOUCH.MD).
8. A pointer `lv_indev` reading POINT, on that display.

The adapter never sets BACKLIGHT: the driver turns the picture on cleared and at `brightness_default`; the app and the VM own the backlight.

## The task and the lock

One LVGL task (`CONFIG_SYS_LVGL_TASK_PRIO`, below the router's work task). It wakes on a notification or LVGL's next timer (at most a refresh period: INSTALL's `refresh_hz`, 41 ms at the default 24 Hz), and under the LVGL lock takes its bits, then runs `lv_timer_handler()` while RUNNING; it renders too. So no frame starts after STOP or RESET returns. LVGL has no OS of its own (`LV_OS_NONE`): the LVGL lock is sys_lvgl's recursive mutex, `sys_lvgl_lock()`, and every `lv_*` call is made under it, only while installed.

- Other tasks post to it by bits (`BIT_DISPLAY_STATE`, `BIT_INVALIDATE`, `BIT_GONE`) and a task notification, never by `lv_async_call` (it allocates and makes a timer: it needs the lock).
- Lock order: the LVGL lock, then the registry's (FLUSH, POINT run under both). Never the LVGL lock under the registry lock: the registry's event listener posts only.

## FLUSH and its end

- `flush_cb` calls FLUSH with `px_map`, `len` = the area's bytes, the area, `done` and the FLUSH's number as `done_ctx`. Before it, a give left by a FLUSH given up on is taken.
- Accepted: the FLUSH is in flight. `flush_wait_cb` (LVGL calls it before the next FLUSH and before it writes into a buffer in flight) waits for `done`'s semaphore, at most `CONFIG_SYS_LVGL_FLUSH_TIMEOUT_MS`; LVGL clears its flag itself. STOP, RESET, ROTATION and the display's state change wait the same way.
- `done` (the ISR) gives the semaphore only for the FLUSH awaited (its number), so a late `done` of a FLUSH given up on is ignored.
- Refused: `lv_display_flush_ready` at once, the screen redrawn after this refresh (a bit: LVGL asserts on an invalidation during rendering), the error reported once while FLUSHes keep failing (until one is accepted). `ERR_DEV_SUSPENDED` is not reported: it means PAUSED.
- A RESET of the display from elsewhere (the app's DEVICE RESET, a sweep, the error policy) drops the transfer in flight, and no `done` comes for it: it ends the FLUSH awaited (runit's state listener, "The display's state", gives the semaphore), and the next FLUSH first sets ORIENTATION again (the rest of the frame is drawn in it). A panel's RESET can outlast the limit: past it the adapter reads the display's INFO, which waits behind that RESET (the registry lock), and when a RESET ended the FLUSH that is no timeout.
- No `done` within the limit: `ERR_DISPLAY_FLUSH_TIMEOUT{id, timeout_ms}`, a RESET of the display (it drops the hung transfer: no `done` after it), its ORIENTATION set again at once (the rest of the frame is drawn in it), the screen redrawn.

`flushes` and `flush_errors` in STATUS count since INSTALL.

## The display's state

INSTALL subscribes (`sys_device_events.h`, inline listeners: they only post; the handles are kept in the adapter's state) to the registry's events of the display, of a sweep (`SYS_DEVICE_ID_NONE`: SUSPEND, RESUME, RESET of every device) and, when there is a touch, to its REMOVED; RESET, or a failed INSTALL, unsubscribes them: an empty adapter listens to nothing, an installed one only to its own devices. SUSPENDED, RESUMED and RESET post `BIT_DISPLAY_STATE`; on a RESET it also ends the FLUSH in flight and marks the rotation to set before the next FLUSH ("FLUSH and its end"). The task then reads the display's INFO: suspended → PAUSED; otherwise (resumed, reset) ORIENTATION set to the adapter's rotation again (a RESET took the config's) and the screen redrawn. REMOVED of the display or the touch posts `BIT_GONE`: the task RESETs the adapter (the FLUSH or POINT in between fails once, reported).

## The display's health

Every `CONFIG_SYS_LVGL_HEALTH_MS` (1 s; 0 never) while it draws, the LVGL task reads its display's HEALTH between two FLUSHes (SYS_DISPLAY.MD). `UNKNOWN` (the module cannot be read): not checked again until the next INSTALL. `NO_ANSWER`: `ERR_DISPLAY_LOST` reported once; when the panel answers again the screen is redrawn (a loose data wire leaves the picture wrong). `RESTARTED` (it lost its power and woke in its power-on state): `ERR_DISPLAY_LOST` (unless already reported), a RESET of the display, its ORIENTATION set again, the screen redrawn.

## The touch

`read_cb` reads POINT every refresh period. A failed read answers released and is reported once, until a read works. A suspended touch answers not pressed (SYS_TOUCH.MD), no error.

## ROTATION

Waits for the FLUSH in flight, then DISPLAY ORIENTATION (refused while a FLUSH is in flight, so never mid-transfer), then `lv_display_set_rotation`, which swaps LVGL's width and height and redraws the screen. While PAUSED the suspended display refuses ORIENTATION: only LVGL turns, and the display takes the rotation when it resumes ("The display's state"). It works in every state but EMPTY. A layout drawn for one size does not reflow by itself to the other: that is the screen's (`sys_screen`'s) business.

## RESET

Waits for the FLUSH in flight, publishes `LVGL_EV_RESET` ("Events": the upper layer drops its widget table, its timers and its LVGL pointers), deletes the touch and the display, stops LVGL (`lv_deinit`), frees its pool and the buffers. Nothing of the screen stays: the next INSTALL starts LVGL again on a new pool. A REMOVE of the display or the touch while installed RESETs it (above).

## Events

`LVGL_EVENTS` in `sys_lvgl.h`: module `MODULE_ID_LVGL` (0x29), lane `SYS`, source and channel 0. The codes are wire format: never renumber.

| Event | Flags | Payload | When |
|---|---|---|---|
| `LVGL_EV_RESET` (0x01) | – | – | RESET of an installed adapter, before the touch and the display are deleted: the LVGL objects are still there |
| `LVGL_EV_STATE` (0x02) | NOTIFY | `{u8 state}` `sys_lvgl_state_e` | the state became another (INSTALL, START, STOP, the display suspended or resumed, RESET), once per change |
| `LVGL_EV_ROTATION` (0x03) | NOTIFY | `{u8 rotation}` `sys_display_orientation_e` | ROTATION worked |

RESET is published inline, under the LVGL lock: its listener (`sys_screen`) runs inline, drops what it made in LVGL (widgets, timers) and returns; it calls no one that waits and takes no lock but the LVGL one. A queued listener would run after the display is deleted: not for this event. STATE and ROTATION are published deferred (`SYS_EVENT_PUBLISH_DEFER`, in the lane's task): the LVGL task never waits for their listeners or for the events stream.

## The pool

Every widget, style, layer, text and cache of LVGL is in **one pool**, managed by LVGL's own allocator (TLSF, `LV_STDLIB_BUILTIN`), never in the system heap: the screen cannot fragment the firmware's heap, and the firmware cannot take the screen's memory once INSTALL has it.

- INSTALL takes one block of `heap_bytes` (internal RAM, 8-byte aligned). `lv_init` builds LVGL's heap on its first `LV_MEM_SIZE` (32 KB) through `LV_MEM_POOL_ALLOC` (`sys_lvgl_pool_take`), and the adapter adds the rest of the block as a second pool (`lv_mem_add_pool`). LVGL 9.3 sizes TLSF's index by the constant `LV_MEM_SIZE + LV_MEM_POOL_EXPAND_SIZE` (`lv_tlsf.c`), hence the two pools in one block and the 4 MB limit.
- RESET runs `lv_deinit` and frees the block: whatever LVGL still held (its theme, caches) goes with it.
- STATUS: `heap_budget` is the pools as TLSF counts them (the block less its bookkeeping, about 4 KB of 64 KB), `heap_used`, `heap_peak` since INSTALL; 0 when EMPTY. LVGL itself and an empty screen use about 9 KB; `heap_budget` drifts down a little as blocks are made (TLSF's per-block overhead), so a free figure is read when it is needed, not kept.
- LVGL does not survive a full pool: a failed allocation ends in its assert (the handler `sys_lvgl_assert_failed` logs and aborts) or, where LVGL does not check (a parent's child list grown by `lv_realloc`), in a NULL dereference; on the board both are a panic and a restart. Turning the asserts off does not help: LVGL would use the NULL further on. So LVGL never reaches the end of the pool: before anything that makes it allocate (a widget, a longer text) the layer that makes widgets calls `sys_lvgl_pool_check(need)`, which refuses (`ERR_LVGL_POOL_FULL{free, need, reserve}`) when `need` would leave less than `CONFIG_SYS_LVGL_POOL_RESERVE` (8 KB) in the pool's largest free block; the reserve is for what LVGL allocates by itself while it draws (layers, label lines). Measured in the simulator (`scenarios/pool_full.txt`): a label takes about 240 B; the smallest pool (32 KB) holds LVGL, an empty screen and 39 labels before the refusal, and the screen goes on drawing and turning.

## LVGL configuration

`config/`: CMakeLists.txt points LVGL at `config/lv_conf.h` (`LV_CONF_PATH`), puts `config/` on LVGL's include path (its sources include the hooks) and sets `LV_KCONFIG_IGNORE`, so the sdkconfig's LVGL menu is not used (LVGL 9.3's Kconfig has no switch for `LV_DRAW_SW_SUPPORT_RGB565_SWAPPED`). `lv_conf.h` says what LVGL has, one section per subject, and includes the list of widgets; whatever neither sets is LVGL's default (`lv_conf_internal.h`):

| Where | What LVGL has |
|---|---|
| `lv_conf.h`, drawing | Colour depth 16 for LVGL's own layers, RGB565_SWAPPED and RGB888 renderers, stride alignment 1, buffer alignment 4, the default refresh `SYS_LVGL_REFRESH_HZ` 24 Hz (`LV_DEF_REFR_PERIOD` 41 ms; the touch read as often), INSTALL's `refresh_hz` sets another |
| `lv_conf.h`, memory | The pool ("The pool"): LVGL's allocator on INSTALL's block, `LV_MEM_SIZE` 32 KB, at most 4 MB |
| `lv_conf.h`, system | `LV_OS_NONE`, log at WARN through `ESP_LOG`, the assert handler |
| `lv_conf.h`, fonts | Montserrat 12, 14 (default), 16 and 24: the screen layer's font enum |
| `lv_conf_widgets.h` | Every widget listed on or off. On: arc, bar, button, buttonmatrix (the keyboard's), chart, checkbox, image, keyboard, label, led, slider, spinbox, switch, textarea. Off: the rest (no flash). Flex layout, no grid; the default theme only |

A widget, a font or a layout that is off does not exist on the board: the screen layer offers only what is on. LVGL's own examples and demos are not built (`CONFIG_LV_BUILD_EXAMPLES`, `CONFIG_LV_BUILD_DEMOS` off in `sdkconfig`): they use widgets that are off.

## Errors (`ERR_MOD_SYS_LVGL`)

| Tag | Meaning |
|---|---|
| `ERR_LVGL_INSTALLED` | INSTALL while installed: RESET first |
| `ERR_LVGL_NOT_INSTALLED` | START, STOP, ROTATION while empty |
| `ERR_LVGL_NOT_DISPLAY{id}` | around the device's error: its GEOMETRY or ORIENTATION failed |
| `ERR_LVGL_NOT_TOUCH{id}` | around the device's error: its POINT failed |
| `ERR_LVGL_FORMAT{id, format}` | a pixel format the adapter does not draw |
| `ERR_LVGL_FLUSH_MAX{id, flush_max, row}` | the display's largest FLUSH is less than one row |
| `ERR_LVGL_NO_MEMORY{bytes}` | no memory for LVGL's pool or a draw buffer (DMA), or LVGL could not make the display (`bytes` 0) |
| `ERR_LVGL_HEAP{heap_bytes, min, max}` | the pool asked for is outside `SYS_LVGL_HEAP_MIN` .. `SYS_LVGL_HEAP_MAX` |
| `ERR_LVGL_POOL_FULL{free, need, reserve}` | `sys_lvgl_pool_check`: what was asked would eat into the reserve |
| `ERR_LVGL_REFRESH{refresh_hz, min, max}` | the refresh asked for is outside `SYS_LVGL_REFRESH_MIN` .. `SYS_LVGL_REFRESH_MAX` (1 .. 60 Hz) |

The display's own errors (`ERR_DISPLAY_*`) are SYS_DISPLAY.MD's; the adapter reports `ERR_DISPLAY_FLUSH_TIMEOUT` and `ERR_DISPLAY_LOST`.

## Simulator

`lvgl-playground/` (its README.md) builds this component, `sys_device`, `sys_display`, `sys_touch`, the router, `cmd` and `errors` from here on the PC, on `test_host`'s stand-ins, with a display driver (`esp_lcd` panel IO, ILI9341) on a simulated SPI bus and controller and a simulated touch, and runs scenarios of command frames against them.

## Open

- `sys_screen` (the widgets from frames) and the VM adapter: `lvgl-playground/sketch/`.
- No display or touch driver on the board yet (`sys_spi` first).
