# sys_system

Spec: `SYS_SYSTEM.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- `MODULE_ID_SYS`, the op codes (`SYS_OPERATIONS` in `sys_system_router.h`), the packed frames and the values of `sys_safe_reason_e` are wire format: never renumber; `contract_sys.json` follows every change of them.
- `sys_system_enter_safe_state` is the one way into the safe state: every module that needs it calls it with its reason (a new cause is a new `sys_safe_reason_e` value), never its own sweep or a hook of its own. It publishes SAFE inline before the sweep: a module that must stop before the devices are suspended (the VM) listens to SAFE, nothing here knows it. A SAFE listener only asks for its stop, it never waits. Never call it from inside a device's op (the sweep takes every device's lock). The dead-man's gone listener never enters it itself (it runs on a driver's RX task or the board timers' task): it notes the reason and has the router's urgent task run it (`router_run_urgent`).
- The events table (`sys_system_events.h`) is wire format like the ops: `contract_sys.json` "events" follows it.
- The dead-man stays runtime only: never stored, so a board that restarts runs its program offline. It is the session's silence: no timer here.
- Timers are board timers (`board_timer_t`), never `esp_timer` or a FreeRTOS timer.
