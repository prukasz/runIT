# sys_device_router

Spec: `SYS_DEVICE_ROUTER.MD` (this folder).

- Every change in this folder updates this file if needed.
- Decoupling invariant: `sys_device`, contracts, and device drivers never depend on `router` or include `sys_device_router.h`. Keep all router wiring in this component.
- Linker requirement: `CMakeLists.txt` must keep `WHOLE_ARCHIVE` so `ROUTER_MODULE_REGISTER(MODULE_ID_SYS_DEVICE, sys_device_router)` is not dropped when nothing calls `sys_device_router` directly.
- Every routed device call must go through `sys_device_call_routed` (never `sys_device_call`), preserving `SYS_DEVICE_OP_LOCAL` (`ERR_DEV_LOCAL_OP`) and `SYS_DEVICE_CREATE_SYSTEM` (`ERR_DEV_SYSTEM`) checks.
- Wire header `sys_device_router_hdr_t` stays 4 B packed (`dir u8`, `id u8`, `op u16`) under `MODULE_ID_SYS_DEVICE` (`0x2F`), copied via `memcpy` for unaligned frames.
