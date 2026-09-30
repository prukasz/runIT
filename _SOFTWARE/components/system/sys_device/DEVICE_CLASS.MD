# Device class as the one descriptor of a device type

Status: **proposal** (2026-10-01), not implemented. Builds on the packet router and the wire-cfg devices (`device_pca9685`, `device_servo`) described in [SYS_DEVICE.MD](SYS_DEVICE.MD). Nothing here changes the wire format.

## Problem

A device type is spread over five places that have to agree by hand:

| Place | What it holds |
|---|---|
| device header | `<CHIP>_TYPE_ID`, the cfg struct (= create frame), the `d_<chip>_create` declaration |
| device `.c` | the `static const sys_device_class_t` and `d_<chip>_create` (NULL check, `PIN_REFS_BELOW`, `SYS_DEVICE_CREATE`) |
| `runit_decoders.c` | `SYS_DEVICE_TYPE_FN(...)` and `sys_device_register_type(ID, sizeof(cfg), wrapper)` |
| `runit_board_cfg.c` | `d_<chip>_create(&(d_<chip>_cfg_t){...})` for onboard chips |
| `sys_device.c` | a `{type_id, cfg_size, create}` table that is not linked to any class |

An instance knows its class, but a class knows neither its `type_id` nor its cfg size. "What type is device 20?" can't be answered, and a device that forgets the pin-order check in its `create` fails only later (suspend / uninstall sweeps assume the rule).

## Proposal

The class carries everything needed to create a device from a frame.

```c
// sys_device.h
typedef struct sys_device_class_t {
  uint8_t type_id;                 // byte after 0x00 in a create frame
  uint8_t cfg_size;                // sizeof(d_<chip>_cfg_t), the wire struct
  const uint8_t* pin_ref_offsets;  // offsetof() of every pin_ref_wire_t field in the cfg
  uint8_t pin_ref_count;
  const char* name;
  const void* contracts[SYS_DEVICE_CONTRACT_MAX];
  sys_device_ops_t ops;
  const sys_device_op_t* dev_ops;
  uint8_t dev_ops_count;
} sys_device_class_t;

// device_pca9685.c (public, const data only)
const sys_device_class_t g_pca9685_class = {
    .type_id = PCA9685_TYPE_ID,
    .cfg_size = sizeof(d_pca9685_cfg_t),
    SYS_DEVICE_PINS(d_pca9685_cfg_t, oe_pin),     // -> .pin_ref_offsets / .pin_ref_count
    .name = "PCA9685_PWM_EXPANDER",
    .contracts = {[SYS_DEVICE_CONTRACT_IO] = &s_pca9685_io_contract},
    .ops = {...},
};
// device_pca9685.h
extern const sys_device_class_t g_pca9685_class;
```

### Creating a device

One generic function replaces every `d_<chip>_create`:

```c
err_h sys_device_create(const sys_device_class_t* cls, const void* cfg, size_t cfg_size);
```

1. `cfg_size != cls->cfg_size` → `ERR_DEV_CREATE_SIZE`.
2. For every offset in `cls->pin_ref_offsets`: the pin must be unused or on a lower-ID device → `ERR_DEV_PIN_ORDER` (the `PIN_REFS_BELOW` rule, now impossible to forget).
3. `sys_device_install(cls, cfg[0], cfg)`.

Callers:

- **Router** (create frame `[0x00][type_id][cfg]`): finds the class by `type_id`, then `sys_device_create(cls, frame + 2, len - 2)`.
- **Board**: `SYS_DEVICE_CREATE(&g_pca9685_class, &(d_pca9685_cfg_t){...})`. The macro passes `sizeof(*cfg_ptr)`, so a wrong cfg type is caught at run time.
- **Registration** (runit, one line per type): `sys_device_register_class(&g_pca9685_class)`. Later a linker-section table can replace it, so adding a device touches no other file.

Range checks that belong to the chip (`SE_CHECK_IN_RANGE`, e.g. `min_us <= home_us <= max_us` of the servo) move into `install`. They are then wrapped by `ERR_DEV_INSTALL_FAILED {dev_id}` like every other install error.

## What disappears

- `d_<chip>_create` (declaration, definition, doc) in every migrated device.
- `SYS_DEVICE_TYPE_FN`, the `type_create_*` wrappers and the `sizeof(...)` in `runit_decoders.c`.
- The separate `device_type_t` table in `sys_device.c` (the registry is the classes themselves).
- The hand-written `PIN_REFS_BELOW` call in each device.

## Why pins by offsets, not a line in `install`

The rule "a device's pins are on a lower-ID device" is system-wide. A check inside each device can be forgotten; a check driven by the class cannot. The same offsets give the device's dependency list for free, which other work needs:

- **Dependency tracking:** `create` records which devices the new one depends on; `sys_device_uninstall` of a used device is refused (`ERR_DEV_IN_USE {dev_id, used_by}`) or cascades from the highest ID. Today `sys_device_uninstall` checks nothing, so removing PCA9685 leaves a servo with a dangling pin that fails at its next write.
- **Introspection:** `cls->type_id` makes "list devices" possible (`[id, type_id, state, onboard]`), so the app can rebuild its view after a reconnect. No current packet (0x10–0x1C) returns the installed devices.

## Compatibility during migration

Devices that still have a decoder keep using `sys_device_register_type(type_id, size, create_fn)`. Both registries coexist until the last device is migrated; then `sys_device_register_type`, `dec_sys_device_install.h` and the `dec_device_<chip>.h` files go.

The generator is unchanged: `@type-id` in the `//#device` record still names the byte, and the class uses the same `#define` (`.type_id = PCA9685_TYPE_ID`), so the number is written once.

## Costs and limits

- The class becomes a public symbol (`g_<chip>_class`). It holds only `const` data.
- `cfg_size` is `uint8_t`; every cfg is a few bytes today. A cfg with a variable length would need a different path.
- `SYS_DEVICE_PINS` relies on `offsetof` of packed fields; the struct is `__packed`, so the offsets are the wire offsets.

## Steps

1. New class fields, `sys_device_create`, class registry; the router create path uses it.
2. `device_pca9685` and `device_servo` on the new shape; the board creates PCA9685 through `SYS_DEVICE_CREATE(&g_pca9685_class, ...)`.
3. Remove `d_pca9685_create`, `d_servo_create` and `SYS_DEVICE_TYPE_FN`.
4. Each remaining device joins when it is migrated (merged driver and adapter, wire cfg, own header).

## Open questions

- Dependency tracking: refuse the uninstall, or cascade? (Refusing is the safer default; the app can offer "remove with dependents".)
- Is the linker-section registration worth its `linker.lf` and whole-archive setup, or is one `register_class` line per device acceptable?
