# CODECS — Packet Decoder / Encoder Tables

`codecs` is a **header-only** component holding the packet tables that [[SYS_INTERFACE.MD]] routes to. It has no source files and produces no library — `idf_component_register()` only exports `decoders/` and `encoders/` as include directories.

| Directory | Contents |
| :--- | :--- |
| `decoders/` | Inbound packet tables — one header per class byte. |
| `encoders/` | Outbound packet builders (currently empty). |

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
  return sys_io_toggle(packet->device_id, packet->pin);
}
```

...plus a row in the central `X`-macro list:

```c
#define SYS_CONTRACTS_PACKET_LIST(X) \
  ...                                 \
  X(HEADER_packet_sys_io_toggle_t, packet_sys_io_toggle_t, decoder_packet_sys_io_toggle_t)
```

`SYS_CONTRACTS_PACKET_LIST` expands through `SYS_CONTRACTS_DECODE_CASE` into the `switch` inside `dec_sys_contracts_decode()`, which is what `sys_interface_init()` registers for class `0x01`. Each case declares the payload struct on the stack, fills it via `convert_to_packet()` (length-checked), and calls the decoder.

Every decoder logs what it is about to do at `ESP_LOGI` under the tag `dec_sys_contracts`. Getter packets log the *result* on a second line, since the value has nowhere else to go until an encoder path exists.

### Packet Byte Ranges

| Range | Domain | API |
| :--- | :--- | :--- |
| `0x10`–`0x19` | Device lifecycle | `sys_device_uninstall/reset/suspend/resume/freeze/sync` (+ `_all` variants) |
| `0x20`–`0x2F` | IO | `sys_io_reset/set_mode/set_level/get_level/toggle/get_voltage/set_voltage/set_pwm_frequency/set_pwm_duty` |
| `0x30`–`0x3F` | Power | `sys_power_budget_update_source`, `sys_vreg_set_enable/voltage/current`, `sys_power_monitor_get_voltage/current`, `sys_power_usb_pd_set/list/get_limits` |

Unmapped packet bytes raise `ERR_INTERFACE_UNKNOWN_PACKET` (owner `OWNER_DEC_SYS_CONTRACTS`) and log a warning.

---

## Adding a New Class

Copy the shape of `dec_sys_contracts.h`:

1. Create `decoders/dec_<name>.h` with a `#define <NAME>_CLASS_HEADER 0xXX`.
2. Set `#undef OWNER` / `#define OWNER OWNER_<NAME>` near the top so `SE_*` macros tag errors correctly, and add that owner to the module's `sys_error_*.h` owner map.
3. Expose one `static inline err_h dec_<name>_decode(const uint8_t* data, size_t len)` — `data[0]` is the packet byte.
4. Register it at boot: `sys_interface_register_class(<NAME>_CLASS_HEADER, dec_<name>_decode, "<name>")`.

Note that a decoder header defines `OWNER` for the rest of the translation unit; any `.c` including it must re-`#define OWNER` afterwards (as `sys_interface.c` does).
