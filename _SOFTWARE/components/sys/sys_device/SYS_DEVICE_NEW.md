# sys_device — adding a device

How to write a device driver on the registry. The registry itself (CREATE, REMOVE, ops, sweeps, dependencies, locks, error policy) is in `SYS_DEVICE.MD`; the contract tables that the handlers below use are described there too ("Contract tables").

One component, `components/devices/device_<chip>/`. Reference: `device_pca9685`.

1. **Type ID:** a line in `sys_device_types.h` (`X(<CHIP>, 0x00..)`), next free number.
2. **Header:** the create struct (packed, the wire form) and the chip's limits. Nothing else is exported.
3. **Struct:** `sys_device_base_t base` first (`id`, `suspended`, the declared dependencies), then what the device needs. The registry allocates it zeroed and fills `id`.
4. **Ops** with the contract handler macros (`DEVICE_HANDLER(RESET)`, `IO_HANDLER(SET_LEVEL)`, `PINS_HANDLER(LOCK)`, rows `DEVICE_ENTRY` / `IO_ENTRY` / `PINS_ENTRY`); `ctx` is the struct. Own ops (`SYS_DEVICE_OWN_OP(n)`) use `SYS_DEVICE_HANDLER(fn, args_t)` and a `{op, fn}` row. Writes start with `ERR_TRY(sys_device_check_active(&c->base))`. Range-check values the chip would reject (`ERR_DEV_RANGE`) and pins (`ERR_DEV_BAD_PIN`); don't clamp. `k_ops` lists neither SUSPEND / RESUME (those are the type's hooks, step 7) nor REMOVE / INFO / OPS / the error-policy ops (the registry answers them); the registry asserts this at boot.
5. **`create(void* ctx, const void* cfg)`:** checks the cfg (ranges), declares every device it uses (`sys_device_use` for each pin's device), fills the struct, builds the hardware, recording each step it built. No allocation, no registration: the registry does both.
6. **`uninstall(void* ctx)`:** releases what was built. Every step runs, guarded by what was built (`SYS_DEVICE_KEEP_FIRST`). Used by REMOVE and by the create rollback; the registry frees the struct.
7. **The `suspend(void* ctx)` and `resume(void* ctx)` hooks** of the type are its SUSPEND and RESUME. SUSPEND must leave every output safe; it is the fault path. The registry owns the `suspended` flag: it is set after the hook, even when the hook fails, and it reads as before the op inside the hook. RESUME runs only while suspended and clears the flag on success. Neither hook checks the flag or sets it.
8. **Register:** `SYS_DEVICE_TYPE_DEFINE(.type = SYS_DEVICE_TYPE_<CHIP>, .cfg_size = sizeof(cfg_t), .ctx_size = sizeof(ctx_t), .create = create, .uninstall = uninstall, SYS_DEVICE_OPS(k_ops))` at the end of the file, and `WHOLE_ARCHIVE` in the component's `CMakeLists.txt` (nothing references the driver; without it the linker drops the registration). Nothing to add in `runit`: every component under `components/devices` is built and linked.

Locking rules for the op bodies (no own lock across `sys_device_call` / `sys_event_publish`, only its own I/O inside an op) are in `SYS_DEVICE.MD` "Concurrency".
