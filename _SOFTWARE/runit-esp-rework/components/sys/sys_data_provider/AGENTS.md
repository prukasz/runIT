# sys_data_provider — agent notes

- Keep `SYS_DATA_PROVIDER.MD` and `errors/errors_sys_data_provider.json` + `router/contract_provider.json` aligned with code changes.
- Wire invariants: `MODULE_ID_PROVIDER = 0x0D` (`ERR_MOD_SYS_DATA_PROVIDER`), `sys_data_provider_type_e` (`u8`), `SYS_DATA_PROVIDER_STATE_*`, `sys_data_provider_info_t` (2 B), `sys_data_provider_entry_t` (3 B), `sys_data_provider_router_hdr_t` (`[dir u8][id u8][op u16]`, 4 B LE), `CONTRACT_PROVIDER_OP_*` (`0x0D00..0x0D07`), own ops `0xFFxx`, and error tags `1..9`, `11..15`.
- Do not add `.destroy` back to `sys_data_provider_t` and do not add X-macro wrapper generators or static `add()` bypasses; drivers set `p->send` inside `create()` and cleanup lives on `sys_data_provider_type_t.remove`.
- The types and the state bits are in `sys_data_provider.h`: no `config/` header.
- Keep `sys_data_provider` lean: avoid heavy per-slot state machines, generation tokens, or resource-stack abstractions.
