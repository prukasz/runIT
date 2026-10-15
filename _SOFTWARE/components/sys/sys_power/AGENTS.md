# sys_power

Spec: `SYS_POWER.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- Do not add references to `legacy docs` unless the user asks for them.
- Op numbers, event numbers and their flags, the enum values (`sys_power_alert_e`, `sys_power_supply_source_e`, `sys_power_usb_pd_option_type_e`), the packed layouts (`sys_power_usb_pd_option_t` is 8 B, `sys_power_usb_pd_limits_t`) and the error tags are wire format: do not renumber or re-layout them. New ones go at the end.
- Changing a contract table, an enum, a layout, an error or a descriptor requires an update in `templates/`: run `sh templates/sync.sh` (regenerates `enums.json` / `errors.json`, refreshes `generated/`, validates). Commit with `--check` green. The op numbers, args structs and field offsets in `contract_power_*.json` must match the tables.
- The contract IDs (`SYS_DEVICE_CONTRACT_POWER_*`) are in `sys_device.h`, not here.
- Event flags come from the table: a power event that the user should see is `SYS_EVENT_NOTIFY` there, never set at a publish call.
- The wrappers in `sys_power.c` only build `sys_device_call`. No device logic goes here.
- `sys_power_mW` stays `int64` in the product: a 20 V × 5.5 A reading overflows a 32-bit product.
- Keep `WHOLE_ARCHIVE` in `CMakeLists.txt`: the error tags are registered by a constructor.
