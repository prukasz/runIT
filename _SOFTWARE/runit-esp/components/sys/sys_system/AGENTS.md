# sys_system

Spec: `SYS_SYSTEM.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- `MODULE_ID_SYS`, the op codes (`SYS_OPERATIONS` in `sys_system_router.h`), the packed frames and the values of `sys_safe_reason_e` are wire format: never renumber; `contract_sys.json` follows every change of them.
- The safe state is every device suspended (`sys_device_sweep`), nothing else here: never call it from inside a device's op (the sweep takes every device's lock). The dead-man's gone listener never enters it itself (it runs on a driver's RX task or the board timers' task): it notes the reason and has the router's urgent task run it (`router_run_urgent`).
- The dead-man stays runtime only: never stored, so a board that restarts runs its program offline. It is the session's silence: no timer here.
- Timers are board timers (`board_timer_t`), never `esp_timer` or a FreeRTOS timer.
