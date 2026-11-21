# sys_device_policy

Spec: `SYS_DEVICE_POLICY.MD` (this folder).

- Every change in this folder updates this file if needed.
- Decoupling & locking invariants:
  - `sys_device` never depends on or calls `sys_device_policy`.
  - Policy state (`s_pol`) uses only a short critical section (`s_mux`), never registry locks; `sys_device_policy_handle()` must never call device ops or `sys_device_sweep()` synchronously on the reporting task (a caller inside a device op may hold a slot lock).
- Generation safety: `SYS_DEVICE_EV_REMOVED` only marks a slot absent when its generation byte matches the `CREATED` generation recorded for that slot.
- Recursion guards: keep the `ERR_DEV_FAULT_RESPONSE` chain check and `xTaskGetCurrentTaskHandle() == sys_device_policy_task_h` check intact so response failures are logged and never re-queued.
- Router (`router/sys_device_policy_router.c`): root key `MODULE_ID_POLICY`, `POLICY_OPERATIONS` in `router/sys_device_policy_router.h` with `ROUTER_READ` / `ROUTER_WRITE` mirror semantics (`sys_device_policy_entry_t`). Keep `CMakeLists.txt` `WHOLE_ARCHIVE` so `ROUTER_MODULE_REGISTER(MODULE_ID_POLICY, sys_device_policy_router)` stays linked.
