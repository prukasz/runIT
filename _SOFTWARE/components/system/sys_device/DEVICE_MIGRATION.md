# Migrating a device to the class-based pattern

`pca9685` (`components/devices/device_pca9685/`) is the reference
implementation. This is the checklist to bring another device in line with it.
The legacy path (`sys_device_install`, `SYS_DEV_ARG_PACK`/`UNPACK`,
`void** install_args`) keeps working untouched, so devices can migrate one at a
time in any order.

## 1. Header: replace positional args with a typed cfg struct

Replace the `d_*_create(...)` positional parameter list with one struct. Collapse
every `(io_device, io_num, io_mode)` triple into a `sys_io_pin_ref_t`.

```c
typedef struct d_<dev>_cfg_t {
  uint8_t device_id;       // MUST be first member - SYS_DEVICE_CREATE reads it
  bool i2c_bus;
  uint8_t i2c_addr;
  sys_io_pin_ref_t intr_pin;   // one field per former (device, pin, mode) triple
  sys_io_pin_ref_t reset_pin;  // ... name each by what it does, not "pin2"
} d_<dev>_cfg_t;

err_h d_<dev>_create(const d_<dev>_cfg_t* cfg);
```

**An unused pin MUST be spelled `SYS_IO_PIN_NONE` explicitly.** Omitting the
field zero-fills it to `{device 0, pin 0, SYS_IO_MODE_INPUT}` — a real device
and a real pin, not "unused". Say so in the struct's doc comment.

## 2. Adapter context: embed the cfg, drop the old fields

```c
typedef struct <dev>_adapter_ctx_t {
  sys_device_adapter_base_t base;  // must be first
  d_<dev>_cfg_t cfg;               // value copy; the ONLY cfg the adapter reads

  /* ...existing device-specific state (frozen values, etc.) unchanged... */
} <dev>_adapter_ctx_t;
```

Delete any `oe_device_id`/`intr_pin_num`/etc. fields the ctx used to carry —
they're now `ctx->cfg.<name>`.

Add an install-steps enum for whatever this device's install does conditionally
(pin setup, i2c add, second driver registration, ...):

```c
enum { <DEV>_STEP_I2C_ADDED = 0, <DEV>_STEP_INTR_READY = 1, /* ... */ };
```

## 3. `device_install`: cfg pointer in, `SYS_DEV_CTX_NEW` + `SYS_DEV_INSTALL_STEP`

```c
static err_h device_install(const void* cfg_blob, void** out_device_handle) {
  const d_<dev>_cfg_t* cfg = (const d_<dev>_cfg_t*)cfg_blob;
  SE_CHECK_NOT_NULL(cfg);
  SE_CHECK_NOT_NULL(out_device_handle);

  SYS_DEV_CTX_NEW(<dev>_adapter_ctx_t, ctx, cfg);
  err_h err = NULL;

  ctx->base.hw_handle = <dev>_new(ctx->cfg.i2c_addr, ctx->cfg.i2c_bus);
  if (!ctx->base.hw_handle) { free(ctx); SE_RET_ERR(ERR_BASE_NO_MEM, 0); }

  SYS_DEV_INSTALL_STEP(sys_i2c_add_driver(ctx->base.hw_handle), "i2c add driver");
  SYS_DEV_STEP_DONE(ctx, <DEV>_STEP_I2C_ADDED);

  /* ... remaining steps, one SYS_DEV_INSTALL_STEP + SYS_DEV_STEP_DONE pair each ... */

  IF_PIN_REF(ctx->cfg.intr_pin) {
    SYS_DEV_INSTALL_STEP(SYS_IO_REF_SET_MODE(ctx->cfg.intr_pin), "intr pin mode");
    SYS_DEV_STEP_DONE(ctx, <DEV>_STEP_INTR_READY);
  }

  *out_device_handle = ctx;
  return NULL;

fail:
  SYS_DEV_INSTALL_FAIL(err, cfg->device_id, out_device_handle, device_uninstall, ctx);
  return NULL;
}
```

**Delete the `sys_io_register_driver` / `sys_power_register_*` calls from
install entirely** — contracts are now declared in the class (step 5), not
registered imperatively. Same for their `sys_io_unregister_driver` /
`sys_power_unregister` counterparts in uninstall — the whole registry entry
(and its `contracts[]`) is freed by `sys_device_uninstall`, so there's nothing
left to unregister.

## 4. `device_uninstall`: accumulate-and-continue, gated on `steps_done`

This function is also the rollback path for a failed install, so:
- **Never early-return.** Use `SYS_DEV_TEARDOWN_STEP(err, expr)` for every step
  and return the accumulated `err` at the end — not the pattern in
  `RET_IF_DEV_ERR`, which aborts the rest of teardown on the first failure.
- **Gate each step on `IF_SYS_DEV_STEP_DONE`.** Rollback from a failed install
  must only undo what actually ran; `ctx->cfg` already holds every field's final
  value from step 2 onward, so there's no sentinel left to infer that from.

```c
static err_h device_uninstall(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(<dev>_adapter_ctx_t, <dev>_handle_t, ctx, hw, handle);
  err_h err = NULL;

  IF_SYS_DEV_STEP_DONE(ctx, <DEV>_STEP_INTR_READY) {
    SYS_IO_REF_UNLOCK(ctx->cfg.intr_pin);
    SYS_DEV_TEARDOWN_STEP(err, SYS_IO_REF_RESET(ctx->cfg.intr_pin));
  }
  if (ctx->base.hw_handle) {
    IF_SYS_DEV_STEP_DONE(ctx, <DEV>_STEP_I2C_ADDED) {
      SYS_DEV_TEARDOWN_STEP(err, sys_i2c_remove_driver(ctx->base.hw_handle));
    }
    <dev>_delete(hw);
  }
  free(ctx);
  return err;
}
```

If this device has a hand-written `adapter_install_fallback` / `fallback_install`
duplicate (ads7128, ap33772s, tca6424a do), **delete it** and point `fail:` at
`device_uninstall` like above — that duplicate existed only because the old
install path couldn't rely on a single reusable teardown function.

## 5. Class: declare contracts instead of registering them

One entry per contract this device provides. IO devices use one slot; power
devices may fill several (ap33772s: vreg + usb_pd + monitor all at once,
replacing three `sys_power_register_*` calls and their error-handling blocks).

```c
static const sys_device_class_t s_<dev>_class = {
    .name = "<DEV>_...",
    .roles = SYS_DEV_ROLE_IO,          // or SYS_DEV_ROLE_PWR, or both OR'd together
    .contracts = {[SYS_DEVICE_CONTRACT_IO] = (void*)&io_<dev>_vtable},
    .ops = {.install = device_install, .uninstall = device_uninstall,
            .reset = device_reset,     .suspend   = device_suspend,
            .resume = device_resume,   .freeze    = device_freeze,
            .sync = device_sync,       .error_handler = device_error_handler},
};

err_h d_<dev>_create(const d_<dev>_cfg_t* cfg) {
  SE_CHECK_NOT_NULL(cfg);
  return SYS_DEVICE_CREATE(&s_<dev>_class, cfg);
}
```

`contracts[]` is `void*`, not `const void*` — `sys_io_vtable_t` has a mutable
`protected_pins` field. Power contracts are declared `const`, so cast with
`(void*)&s_<dev>_contract` same as before.

## 6. `device_suspend`/`device_resume`/etc: rename fields only

Everything that isn't install/uninstall/class is a mechanical rename:
`ctx->oe_pin_num` → `ctx->cfg.oe_pin`, `IF_PIN(x)` → `IF_PIN_REF(ctx->cfg.x)`,
`WITH_PIN_UNLOCKED(dev, pin)` → `WITH_REF_UNLOCKED(ctx->cfg.x)`,
`SYS_IO_HIGH/LOW(dev, pin)` → `SYS_IO_REF_HIGH/LOW(ctx->cfg.x)`. Logic is
unchanged.

## 7. Board config: designated initializer

```c
d_<dev>_create(&(d_<dev>_cfg_t){
    .device_id = DEVICE_ID_<DEV>,
    .i2c_bus   = SYS_I2C_BUS_INTERNAL,
    .i2c_addr  = 0x..,
    .intr_pin  = SYS_IO_PIN(DEVICE_ID_GPIO_ESP, 9, SYS_IO_MODE_INPUT),
    .reset_pin = SYS_IO_PIN_NONE,
});
```

## 8. Build after each device

One device per commit. `idf.py build` must stay clean (0 warnings) after each —
the legacy path and the other unmigrated devices must not be affected.

## Suggested order (easiest first)

dac53202 (3 args) → gpio_esp (1 arg; also fixes its ctx being a static
singleton instead of `calloc`'d) → ap33772s (first 3-contract case) → ads7128
(also fixes its packed-arg index inconsistencies) → ina3221 → tps55289 →
tca6424a (already had its uninstall leak fixed independently — see git log).

## Once every device is migrated

Delete the legacy path entirely: `sys_device_install`, `install_device`,
`install_args`, `name`, the 7 inline lifecycle fn pointers on `sys_device_t`,
`SYS_DEV_ARG_PACK`/`UNPACK`/`UNPACK_VAL`, and the `DEV_OP`/`DEV_NAME` resolvers
in `sys_device.c` (each collapses to `dev->cls->ops.<fn>` / `dev->cls->name`).
