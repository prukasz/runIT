# CODECS — Packet Decoder Tables

`codecs` is a **header-only** component holding the packet tables that [[SYS_INTERFACE.MD]] routes to. It has no source files and produces no library — `idf_component_register()` only exports `decoders/` (inbound packet tables, one header per class byte).

Every class is registered with `sys_interface` by runit's `runit_register_decoders` boot step (`components/runit/runit_decoders.c`), before `sys_interface_init()` starts the RX receiver. `sys_interface` itself doesn't depend on `codecs`.

The error-chain encoder that used to live in `encoders/` is private to `sys_errors` now (`components/sys_errors/enc_sys_errors.h`); its wire format is in [[SYS_ERRORS.MD]] "Logging and telemetry".

The split of responsibility: `sys_interface` owns the **class byte** (`0xXX`) and the transport plumbing; a codec header owns its **packet bytes** (`0xYY`), payload layouts, and the mapping to `sys_*` API calls.

---

## `decoders/dec_sys_contracts.h` — class `0x01`

Maps packet bytes onto the system contract APIs (`sys_device`, `sys_io`, `sys_power`). Everything is `static inline` in one header, so adding a packet is a single-file change with no build-system edits.

Each entry is three pieces:

```c
#define HEADER_packet_sys_io_toggle_t 0x24          // 1. packet byte
typedef struct __packed {                            // 2. payload layout
  uint8_t device_id;
  uint8_t pin;
} packet_sys_io_toggle_t;

static inline err_h decoder_packet_sys_io_toggle_t(packet_sys_io_toggle_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "toggling io (dev %u, pin %u)", packet->device_id, packet->pin);
  return sys_io_toggle(SYS_IO_REF(packet->device_id, packet->pin));
}
```

...plus a row in the central `X`-macro list:

```c
#define SYS_CONTRACTS_PACKET_LIST(X) \
  ...                                 \
  X(HEADER_packet_sys_io_toggle_t, packet_sys_io_toggle_t, decoder_packet_sys_io_toggle_t)
```

`SYS_CONTRACTS_PACKET_LIST` expands through `SYS_CONTRACTS_DECODE_CASE` into the `switch` inside `dec_sys_contracts_decode()`, which runit registers for class `0x01`. Each case declares the payload struct on the stack, fills it via `convert_to_packet()` (length-checked), and calls the decoder.

Every decoder logs what it is about to do at `ESP_LOGI` under the tag `dec_sys_contracts`. A getter returns its result in the command's response: it declares `packet_<name>_response_t` (packed, scalars or fixed scalar arrays, same field annotations) next to its request struct and calls `sys_interface_respond(&rsp, sizeof(rsp))`. Every command is answered (`[0x05][seq][class][packet][status][data]`, see [[SYS_INTERFACE.MD]] "Responses"). The contracts generator publishes the response layout.

### Packet Byte Ranges

| Range | Domain | API |
| :--- | :--- | :--- |
| `0x10`–`0x19` | Device lifecycle | `sys_device_user_uninstall` (onboard devices are refused), `reset/suspend/resume/freeze/sync` (+ `_all` variants; `0x1C` → `sys_device_user_uninstall_all`, which skips onboard devices) |
| `0x1A` | Device error handling | `sys_device_set_error_handling` — `device_id, importance, actions[5]`; see [[SYS_DEVICE.MD]]'s "Per-Instance Error Handling" |
| `0x1B`–`0x1F` | *(free)* | |
| `0x20`–`0x2F` | IO | `sys_io_reset/set_mode/set_level/get_level/toggle/get_voltage/set_voltage/set_pwm_frequency/set_pwm_duty`, `0x29`: `sys_io_configure_intr` (mode, debounce, ADC window; arms the pin only, events are linked with class `0x09`) |
| `0x31`–`0x3E` | Power | `sys_power_vreg_set_enable/voltage/current` (budgeted, current 0 = automatic), `sys_power_monitor_get_voltage/current`, `sys_power_usb_pd_set/list/get_limits`, `0x39`: `sys_power_monitor_set_alert` (current alert threshold), `0x3C`: `sys_power_get_status` (response), `0x3D`: `sys_power_set_response`. `0x30`, `0x3A`, `0x3B` and `0x3E` are free. Persisted PSU / battery settings are settings class `0x08` (`dec_settings_power.h`). |
| `0x50`–`0x56` | H-bridge | `sys_hbridge_set_mode/set_drive/brake/coast/set_current_limit_mA`, `0x56`: `sys_hbridge_clear_fault`. `0x55` is free (faults are events, class `0x09`). |
| `0x40`–`0x47` | Device installation | `d_gpio_esp_create`, `d_pca9685_create`, `d_tca6424a_create`, `d_tps55289_create`, `d_ina3221_create`, `d_ap33772s_create`, `d_dac53202_create`, `d_ads7128_create` |

Unmapped packet bytes raise `ERR_INTERFACE_UNKNOWN_PACKET` (owner `OWNER_DEC_SYS_CONTRACTS`) and log a warning.

---

## `decoders/device/dec_device_*.h` — class `0x01`, packets `0x40`–`0x47`

One header per device type, included by `dec_sys_device_install.h`: each payload is that device's own `d_xxx_cfg_t` flattened directly onto the wire in field order. This is what lets a device be attached dynamically (at boot via a [[SYS_ACTIONS.MD]] boot action, or any time after) instead of only through the hardcoded `*_static_config()` calls in `runit.c`. The per-device header is also the source of generated API metadata, so its field annotations and documentation stay next to the implementation.

Still the **same class byte** (`0x01`) and the **same dispatcher** (`dec_sys_contracts_decode()`) as `dec_sys_contracts.h` — `dec_sys_contracts.h` just `#include`s this file and folds its `SYS_CONTRACTS_INSTALL_PACKET_LIST(X)` into its own `SYS_CONTRACTS_PACKET_LIST(X)`, the same way `sys_error_codes.h` folds several modules' maps into one `SYS_ERROR_MAP(X)`. The split exists purely so `dec_sys_contracts.h` doesn't have to carry all 8 device headers' includes; it changes nothing about the wire format. Tagged `OWNER_DEC_SYS_DEVICE_INSTALL` (`0xA612`), logged under `dec_sys_device_install` — distinct from `dec_sys_contracts`'s own tag/owner, since it's a separate file.

**No nested struct fields in any packet payload — scalars only.** `sys_io_pin_ref_t` (inter-device pin dependencies — e.g. a regulator's enable pin, wired through a GPIO expander) is not `__packed` (its `sys_io_mode_e` field is int-sized) and, even if it were, would still be a nested type inside the payload. Every pin reference is instead flattened to three plain fields, named `<name>_device_id`, `<name>_pin`, `<name>_mode`, reassembled into a real `sys_io_pin_ref_t` only in the decoder via `pin_ref_from_wire(device_id, pin, mode)`. This keeps every payload struct walkable as a flat list of `uint8_t`/`uint16_t`/`uint32_t`/`bool` fields — deliberately, so future tooling that generates encoders/decoders from these payloads never has to special-case a nested type.

```c
static inline sys_io_pin_ref_t pin_ref_from_wire(uint8_t device_id, uint8_t pin, uint8_t mode);
```

**An unused pin must be encoded as `pin == SYS_GPIO_NONE`** (`SYS_GPIO_NONE` = `0xFF`) — `device_id`/`mode` are ignored whenever that's true, exactly like `SYS_IO_PIN_NONE` on the C side. Sending all-zero fields means "device 0, pin 0" — a real pin, not "none".

| Packet | Byte | Payload (after `device_id`, `i2c_bus`, `i2c_addr` where present) |
| :--- | :--- | :--- |
| Install `gpio_esp` | `0x40` | *(none — `device_id` only)* |
| Install `pca9685` | `0x41` | `oe_pin_device_id, oe_pin_pin, oe_pin_mode` |
| Install `tca6424a` | `0x42` | `intr_pin_{device_id,pin,mode}, rst_pin_{device_id,pin,mode}` |
| Install `tps55289` | `0x43` | `intr_pin_{device_id,pin,mode}, en_pin_{device_id,pin,mode}` |
| Install `ina3221` | `0x44` | `crit_pin_{device_id,pin,mode}, warn_pin_{device_id,pin,mode}` |
| Install `ap33772s` | `0x45` | `intr_pin_{device_id,pin,mode}` |
| Install `dac53202` | `0x46` | *(none)* |
| Install `ads7128` | `0x47` | `intr_pin_{device_id,pin,mode}; uint32_t vref_mV` |

`i2c_bus` is a single byte (`0` = `SYS_I2C_BUS0`/internal, `1` = `SYS_I2C_BUS1`/external). Example — install a PCA9685 as device 5 on the internal bus at I2C address `0x40`, no OE pin: `01 41 05 00 40 00 FF 00`.

---

## `decoders/dec_sys_actions.h` — class `0x03`

Control-packet table for [[SYS_ACTIONS.MD]]'s record/stop/remove wire protocol. Same shape as `dec_sys_contracts.h` (a `SYS_ACTIONS_PACKET_LIST` X-macro expanded into a switch), just with `sys_actions.h` in place of `sys_io.h`/`sys_device.h`/`sys_power.h` as the thing being wrapped.

| Packet | Byte | Payload | Calls |
| :--- | :--- | :--- | :--- |
| Static Invoke | `0x00` | `uint8_t id` | `sys_actions_invoke(SYS_ACTION_SCOPE_STATIC, id)` |
| Dynamic Invoke | `0x01` | `uint8_t id` | `sys_actions_invoke(SYS_ACTION_SCOPE_DYNAMIC, id)` |
| Record Start | `0x02` | `uint8_t id` | `sys_action_record_start(id)` |
| Record Stop | `0x03` | *(empty)* | `sys_action_record_stop()` (persists to NVS) |
| Remove | `0x04` | `uint8_t id` | `sys_action_remove(id)` |
| Remove All | `0x05` | *(empty)* | `sys_action_remove_all()` |

`CONFIG_RX_PACKET_CLASS_SYS_ACTIONS` (`0x03`) and every other class/packet wire-identity byte are Kconfig options under `components/utils/Kconfig`'s "System-Wide Identity Assignments" menu, not `#define`s in any one file — see that menu's "RX/TX Packet Classes" and "RX/TX Packet Headers" submenus for the full byte-0/byte-1 naming scheme. `dec_sys_actions.h` includes `sys_actions.h` for the rest of that component's own API, not for this constant.

Registered by runit like every other class. There is no separate "bind action to state" class anymore — the `sys_states` component that used to own it was folded into `sys_actions` (a hardcoded behavior is now just a bound static function on an action id, see [[SYS_ACTIONS.MD]]); class `0x04` was freed by that change and has since been taken by the VM loader below.

---

## `decoders/dec_vm_loader.h` — class `0x04`

Records are the packed structs of `VM/core/loader/vm_wire.h`, published to `data-structures/vm/vm-program.generated.json`.

Program-upload table for [[VM.MD]]. **Structurally different from the tables above**: these packets are not fixed-size structs, so there is no `X`-macro packet list and no `convert_to_packet()` copy. `0x42`/`0x43` carry a repeating record list whose entries vary in length (inline name bytes, inline payload bytes), so they are walked with an explicit cursor.

| Packet | Byte | Payload | Calls |
| :--- | :--- | :--- | :--- |
| Reset | `0x40` | — | `vm_loader_reset()` |
| Open | `0x41` | `u16 obj_cnt`, `u16 acc_cnt`, `u16 blk_cnt`, `u32 total_size` | `vm_loader_open()` |
| Add objects | `0x42` | `u8 n`, `n ×` `{u16 id, vm_obj_head_t head, char name[head.d.name_size]}` | `vm_loader_add_obj()` per record |
| Set data | `0x43` | `u8 n`, `n ×` `{u16 id, u16 start_idx, u16 byte_len, u8 data[byte_len]}` | `vm_loader_set_data()` per record |
| Add accessors | `0x44` | `u8 n`, `n ×` `{u16 acc_id, u16 root_obj_id, u8 idx_count, u8 idx_len, u8 idx_data[idx_len]}` | `vm_loader_add_accessor()` per record |
| Add block | `0x45` | one block: header, `in_cnt ×` accessor id, `q_cnt ×` object id, `en_cnt ×` accessor id, `custom_len ×` private state | `vm_loader_add_block()` |

`idx_data` holds `idx_count` records of `{u8 kind, payload}` — `LITERAL` a `u32` position, `REF` a `u16` accessor id, `NAME` a `u8` length then that many unterminated bytes. A `REF` target must already exist, which makes reference cycles unconstructable rather than merely depth-capped at resolve time.

`0x45` carries exactly **one** block, unlike everything else here: a block record varies in length four separate ways, so batching would need a length prefix per record and a cursor that can desynchronise. A block names accessors and objects that must already be bound. **Block order is execution order** — the client topologically sorts and uploads in that order.

All multi-byte fields little-endian. Every length is checked against the bytes actually present before use (`DEC_VM_NEED`), raising `ERR_VM_LOAD_SHORT_RECORD` rather than reading past the frame — the cursor walk has no struct-size check to lean on, so this is the only thing standing between a truncated frame and an overread.

`DEC_VM_NEED`'s parameters are named `in_pkt`/`in_off`/`in_len`/`in_need` so plain token substitution cannot collide with the `.packet`/`.need`/`.got` payload designators inside it — the same hazard `SE_CHECK_IN_RANGE` documents in [[SYS_ERRORS.MD]]. Naming them `packet`/`need` expands `.packet = (packet)` into `.0x41 = (0x41)`.

Wire-format rationale (`start_idx` unconditional, `payload_size` in bytes rather than an element count, explicit flags byte rather than the raw header bitfield, child **ids** rather than pointers for `VM_OBJ_PTR`) is documented in [[VM.MD]].

Registered by runit like every other class. The VM can't register it itself: `codecs` `REQUIRES VM` so the decoder can see `vm_loader.h`, which would make a `VM → codecs` dependency circular.

---

## `decoders/dec_features.h` — class `0x05`

Decoder table for higher-order features (servos, H-bridges, etc.):
- `0x01`–`0x0F`: Feature lifecycle (`remove`, `remove_all`).
- `0x10`–`0x1F`: Servo control (`create`, `set_angle`, `go_home`, `set_trim`, `attach/detach`).
- `0x20`–`0x2F`: H-bridge / motor control (`create`, `set_speed`, `brake`, `coast`, `set_current_limit`, `clear_fault`).

Registered by runit for class byte `0x05`.

---

## `decoders/dec_sys_events.h` — class `0x09`

Links events to the program and to actions ([[SYS_EVENT.MD]]). Exposed as the `events` contract catalog.

| Packet | Payload | Response |
|---|---|---|
| `0x01` subscribe | `domain, device_id, channel, event` (255 = any), `route_mask` (bit 0 = program), `static_action_id`, `dynamic_action_id` | `subscription_id` |
| `0x02` unsubscribe | `subscription_id` (255 = every subscription made by packets) | — |

Packet subscriptions are user-owned: the unsubscribe packet can't remove the system's own (device IRQ chaining, power manager, features). Tagged `OWNER_DEC_SYS_EVENTS` (`0xA618`).

---

## Adding a New Class

Copy the shape of `dec_sys_contracts.h`:

1. Create `decoders/dec_<name>.h` and add a `config RX_PACKET_CLASS_<NAME>` (`hex`, `default 0xXX`) to `components/utils/Kconfig`'s "RX Packet Classes" submenu, under "System-Wide Identity Assignments" — do not `#define` the class byte in the decoder file itself.
2. Set `#undef OWNER` / `#define OWNER OWNER_<NAME>` near the top so `SE_*` macros tag errors correctly, and add that owner to the module's `sys_error_*.h` owner map.
3. Expose one `static inline err_h dec_<name>_decode(const uint8_t* data, size_t len)` — `data[0]` is the packet byte.
4. Register it in `runit_register_decoders()` (`components/runit/runit_decoders.c`): `sys_interface_register_decoder(CONFIG_RX_PACKET_CLASS_<NAME>, dec_<name>_decode, "<name>")`.

Per-packet header bytes (byte-1, `data[0]` inside the decoder) stay local `#define HEADER_packet_<struct>_t 0xYY` in the decoder file, as today — only the outer class byte (byte-0) moved to Kconfig, since it's the one value that must stay globally unique across every registered decoder.

Note that a decoder header defines `OWNER` for the rest of the translation unit; any `.c` including it must re-`#define OWNER` afterwards. This is why the registrations live in their own file, `runit_decoders.c`.

**Splitting a class across files.** If one class's packet table grows too large for one header, split it the way `dec_sys_device_install.h` splits off from `dec_sys_contracts.h`: give the split-out file its own `OWNER`/log tag and its own `SOME_NAME_PACKET_LIST(X)`, then have the original file `#include` it and fold that list into its own via `SOME_NAME_PACKET_LIST(X)` as one line inside its own `X`-macro definition (same technique `sys_error_codes.h` uses to combine multiple modules' `SYS_ERROR_*_MAP(X)`s into one `SYS_ERROR_MAP(X)`). The class byte, dispatcher function, and registration call stay in the original file — only packet declarations move.

### VM execution control

`04 48 <command>` is the single execution-control packet; the command is one
byte and no additional payload is accepted. Commands `00` through `08` select
scan mode, execute once, select block mode, execute next, reset to scan start,
select normal mode, pause, resume, and reset/unload respectively. See
`components/VM/VM.MD` for stepping and reset semantics and
`vm_exec_command_e` for the C constants.
