# sys_data_provider

Spec: `SYS_DATA_PROVIDER.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- The provider id (`sys_data_provider_id_t`: `u8 link`, `u32 channel`, packed), the links (`sys_data_provider_link_e`), the state bits (`SYS_DATA_PROVIDER_STATE_*`), `sys_data_provider_info_t`, the router ops and the error tags (`ERR_MOD_SYS_DATA_PROVIDER` = 17) are wire format: never renumber or re-layout them. A change runs `sh templates/sync.sh` (enums, errors, `contract_provider.json`).
- `sys_data_provider.h` is for the users (the session, the connectors), `driver/sys_data_provider_driver.h` for the drivers only (`sys_uart`, `sys_ble`), `internal/` for the router only.
- The registry's spinlock guards a lookup and a copy, nothing more: never call a driver's `send` or `destroy`, or a user's `rx`, under it. `busy` keeps a provider while its driver sends; remove refuses it meanwhile.
- A user's `rx` runs on the driver's RX task: it copies and returns, and sends only with `NOWAIT`. A driver's `send` with `NOWAIT` only queues and raises `ERR_PROV_BUSY` itself when full; the registry wraps any other driver error in `ERR_PROV_SEND`.
- Never cut a frame: longer than `max_frame` is `ERR_PROV_TOO_LONG`.
