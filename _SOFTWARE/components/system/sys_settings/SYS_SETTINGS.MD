# System Settings (sys_settings)

Persistent settings for any module: named binary records in one NVS namespace (`settings`).

## API

| Function | Behaviour |
|---|---|
| `sys_settings_init()` | NVS init + open the namespace. Boot step, before modules load settings. |
| `sys_settings_load(key, out, size, &found)` | `found` is true only if the record exists with exactly `size` bytes. A record stored with another size returns `ERR_SETTINGS_SIZE_MISMATCH {stored, expected}` and leaves `out` untouched, so a changed struct layout reads as "not stored" (use defaults) instead of as garbage. |
| `sys_settings_load_blob(key, out, capacity, &len, &found)` | Variable-length record (its length varies with content): `found` if it exists and fits `capacity`; longer is `ERR_SETTINGS_SIZE_MISMATCH`. |
| `sys_settings_store(key, data, size)` | Write and commit. |
| `sys_settings_erase(key)` | Remove and commit; a missing key is not an error. |

## Rules

- **Keys:** at most 15 characters (NVS), prefixed with the owning module (for example `pwr_psu`).
- **Record layout:** store one fixed-size struct per key. If you change its layout, the old record is ignored (size mismatch), so no migration code is needed for size changes. A same-size layout change needs a new key.
- **Scope:** the module that owns a setting validates it before storing it, and applies it after loading it.

## Users

| Key | Owner | Contents |
|---|---|---|
| `pwr_psu` | sys_power | External supply `{max_mV, max_mA}` |
| `pwr_battery` | sys_power | `{chemistry, cells, low_pct, critical_pct}` |
| `vm_retain` | VM (`vm_retain.c`) | Retained object values, variable length (VM.MD "Retained values") |

`sys_actions` keeps its recorded actions in its own namespace (`sys_actions`).

## Errors

Owners `OWNER_SYS_SETTINGS_*` (`0xB0xx`, map in `errors/sys_error_settings.h`); `ERR_SETTINGS_SIZE_MISMATCH`.
