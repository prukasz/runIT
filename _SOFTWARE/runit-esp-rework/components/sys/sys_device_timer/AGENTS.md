# sys_device_timer

Spec: `SYS_DEVICE_TIMER.MD` (this folder).

- Every change in this folder updates this file if needed.
- Always build on `timer` (`board_timer_t`) and `sys_device_res_add` / `sys_device_step`; never use FreeRTOS or `esp_timer` handles directly.
- `sys_device_timer_init` must only accept `SYS_DEVICE_CONTRACT_OWN` ops (`(op >> 8) == SYS_DEVICE_CONTRACT_OWN`): step calls bypass interceptors and must stay internal to the driver.
- Timer context is packed `[op u16][gen u8][owner u8]` in `void* ctx`, never a pointer to driver state or slots, so an expiry in flight after `REMOVE` or failed `create()` is dropped safely.
- Stopping the timer stays in the resource `release` callback so `REMOVE` and rollback always disarm it before driver memory is released.
