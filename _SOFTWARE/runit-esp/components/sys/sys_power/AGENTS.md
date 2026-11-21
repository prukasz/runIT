# sys_power

Spec: `SYS_POWER.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- Op numbers, event numbers and their flags, the enum values (`sys_power_alert_e`, `sys_power_supply_source_e`, `sys_power_usb_pd_option_type_e`), the packed layouts (`sys_power_usb_pd_option_t` is 8 B, `sys_power_usb_pd_limits_t`) and the error tags are wire format: do not renumber or re-layout them. New ones go at the end.
- Changing a contract table, an enum, a layout, an error or a descriptor requires an update in `templates/`: run `sh templates/sync.sh` (regenerates `enums.json` / `errors.json`, refreshes `runit-app/generated/`, validates). Commit with `--check` green. The op numbers, args structs and field offsets in `contracts/contract_power_*.json` must match the tables.
- The contract IDs are these contracts' own: `MODULE_ID_PMON`, `MODULE_ID_VREG`, `MODULE_ID_PD`, `MODULE_ID_SUPPLY`, each declared once, in its `contracts/sys_power_<name>_id.h` (`MODULE_ID_DECLARE`; the contract's header includes it, then `SYS_DEVICE_CONTRACT_DECLARE`). They are wire format and the modules of the contracts' events: never renumber them.
- Event flags come from the table: a power event that the user should see is `SYS_EVENT_NOTIFY` there, never set at a publish call.
- Plain calls go directly through `SYS_DEVICE_SET` / `SYS_DEVICE_GET`; `sys_power.c` only registers the errors of `MODULE_ID_SYS_POWER` (`ERR_MODULE_DEFINE`) and implements `sys_power_usb_pd_list_options`. No device-specific logic goes here.
- `sys_power_mW` stays `int64` in the product: a 20 V × 5.5 A reading overflows a 32-bit product.
- Keep `WHOLE_ARCHIVE` in `CMakeLists.txt`: the error tags are registered by a constructor.
