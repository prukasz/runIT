# sys_power_manager

Spec: `SYS_POWER_MANAGER.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- Wire format, do not renumber or re-layout: the router ops (`SYS_PWRM_OP_*`: 1, 3, 4, 7, 8, 9, in `sys_power_manager_router.h`), the record IDs `SYS_ACTIONS_ID_POWER_*` (0x01000002 to 0x01000005, 0x01000008), the packed structs (`sys_power_manager_cfg_t` 4 B, `sys_power_pd_t` 5 B, `sys_power_psu_t` 8 B, `sys_power_battery_t` 4 B, `sys_power_status_t` 40 B), the manager event numbers 0 to 5 (0x0600 to 0x0605) with their NOTIFY flags, the kind numbers 1 to 10 (also the matrix index), the response values 0 to 3, the target 0xFFFF, `sys_power_source_e`, `sys_power_battery_e`, `sys_power_pd_mode_e`, the status flags, and the error tags 1 to 6. New ones go at the end.
- The response matrix is saved as the record `SYS_ACTIONS_ID_POWER_RESPONSES`: rows of `sys_power_response_row_t` (`u16 target`, then `SYS_POWER_KIND_COUNT` responses), matched by target. Changing the kind count changes the saved records; a saved row whose rail the board no longer has is refused (`ERR_PWRM_NOT_RAIL`). Say so in SYS_POWER_MANAGER.MD.
- Changing a router op, a struct, an enum, an event, an error or the descriptor requires `sh templates/sync.sh` (regenerates `enums.json` and `errors.json`, refreshes `runit-app/generated/`, validates). Commit with `--check` green. The op numbers, args structs and field offsets in `contract_power_manager.json` must match the code.
- What the board has is the board config's (`sys_power_board_t` in `runit_board_cfg.c`): the rails, the monitor channel of each power element, the source pins, the sink, the hardware limits. What the user chooses is remote: CONFIG (on / off, strict, margin, source without pins), PSU, BATTERY, PD, MATRIX (the responses). The rails' own limits are the regulator's ops (through the guard).
- The board's devices can't be removed (`ERR_DEV_SYSTEM`): a rail never disappears, so nothing here forgets a rail. A rail missing at boot (created later, or its creation failed) counts as off.
- The manager is the only POWER_VREG guard. `sys_device_set_guard` refuses a second one for the same contract. Its own rail changes use `vreg_direct` (`sys_device_call_direct`), never the guarded path, which would call back into the guard.
- Lock order: the guard takes `sys_pwrm_mutex`, then calls the devices directly. The task takes the same mutex. A device op must not call the manager.
- Errors on the task or in the event listener go through `err_report`, never back to a caller. The listener's fault report comes before the response, so the errors stream shows the response taken.
- The board description (`k_power_board` and the consumers in `components/app/runit/runit_board_cfg.c`) is the only source of the limits. `max_mV` and `max_mA` are caps on the budget and on what the rails may take. Changing them changes the budget at once.
- `sys_power_manager_init` checks the board before anything is started. A new board check goes there.
- Keep the 64-bit products in `rail_input_mW`, `limit_of` and the budget: a 20 V × 5.5 A input overflows a 32-bit product.
- `WHOLE_ARCHIVE` in `CMakeLists.txt`: the error tags are registered by a constructor.
