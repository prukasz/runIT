# sys_data_provider — agent notes

- Keep `SYS_DATA_PROVIDER.MD` and `errors/errors_sys_data_provider.json` + `router/contract_provider.json` aligned with code changes.
- Wire invariants: `MODULE_ID_PROVIDER = 0x2B`, `sys_data_provider_type_e` (`u8`), `SYS_DATA_PROVIDER_STATE_*`, `sys_data_provider_router_hdr_t` (`[dir u8][id u8][op u16]`, 4 B LE), and in `router/sys_data_provider_router.c` the op codes `CONTRACT_PROVIDER_OP_*` (`0x2B00..0x2B02`, the names `contract_provider.json` gives), the CREATE header and the LIST row (3 B); own ops `0xFFxx`; error tags `1..5`, `12`, `13`, `15`.
- The public header holds only what C outside uses (the types, the state bits, `sys_data_provider_create_system`, `sys_data_provider_state`); the wire format is the router's (`router/`), the registry knows none of it and the router none of the slots or locks: they meet in `internal/sys_data_provider_internal.h`.
- Do not add `.destroy` back to `sys_data_provider_t` and do not add X-macro wrapper generators or static `add()` bypasses; the base `sys_data_provider_t` holds the port and the state only (the driver's struct starts with it), the registry owns the lifecycle (the slot, the port in the data matrix, one call into a driver at a time) and never looks into a packet: what a line does with it (queues, flags, framing) is the driver's.
- The types and the state bits are in `sys_data_provider.h`: no `config/` header.
- Keep `sys_data_provider` lean: avoid heavy per-slot state machines, generation tokens, or resource-stack abstractions.
