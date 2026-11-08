# sys_data_provider

Spec: `SYS_DATA_PROVIDER.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- The provider id (one byte, only a name: no number means anything and no module compares it with a constant), the links (`sys_data_provider_link_e`), the state bits (`SYS_DATA_PROVIDER_STATE_*`), `sys_data_provider_info_t`, the router ops and the error tags (`ERR_MOD_SYS_DATA_PROVIDER` = 17) are wire format: never renumber or re-layout them. A change runs `sh templates/sync.sh` (enums, errors, `contract_provider.json`).
- `sys_data_provider.h` is for the users (the session, the streams, the command binding of this component), `driver/sys_data_provider_driver.h` for the drivers only (`sys_uart`, `sys_ble`), `internal/` for the router only.
- The registry's spinlock guards a lookup and a copy, nothing more: never call a driver's `send` or `destroy`, or a user's `rx`, under it. `busy` keeps a provider while its driver sends; remove refuses it meanwhile.
- A user's `rx` runs on the driver's RX task: it copies and returns, and sends only with `NOWAIT`. A driver's `send` with `NOWAIT` only queues and raises `ERR_PROV_BUSY` itself when full; the registry wraps any other driver error in `ERR_PROV_SEND`.
- Never cut a frame: longer than `CMD_FRAME_MAX` is `ERR_PROV_TOO_LONG`. No size of a link above its driver: a provider is up or down, framed or raw.
- The command binding (`sys_data_provider_cmd.c`) adds no header: in is the command as the router takes it, out `[status][data]`. Its commands' `src` is `ROUTER_SRC_NONE`; a module that keeps something for a peer refuses it. Its answer finds the binding by the callback's context (slot and generation), never by a pointer the router keeps.
