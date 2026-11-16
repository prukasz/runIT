# sys_data_provider — typed provider registry and router

Component `components/sys/sys_data_provider/`. Keeps the board's ways bytes leave and enter — UART ports, BLE links, MQTT topics, board console — under 1-byte `id` (`0..0xFE`; `SYS_DATA_PROVIDER_ID_NONE = 0xFF` for `LIST`), with one data-plane owner of the RX hook (`open`/`close`/`send`) and a minimal centralized control router (`sys_data_provider_call` / `sys_data_provider_router` under `MODULE_ID_PROVIDER = 0x0D`).

## Driver model

Every provider is created via `CREATE` from a driver registered under `sys_data_provider_type_e` (`u8`: `UART = 1`, `BLE = 2`, `MQTT = 3`, `CONSOLE = 4`) using `SYS_DATA_PROVIDER_TYPE_DEFINE(prov_type, cfg_t, ctx_t, create_fn, remove_fn, ops_ptr, ops_cnt)`:
- `create(sys_data_provider_t* p, void* ctx, const void* cfg)` initializes the instance and sets `p->send` and capability flags on `p->state` (`WIRED`, `FRAMED`, initial `UP`), using `sys_data_provider_deliver()` / `sys_data_provider_set_up()` for RX.
- `remove(sys_data_provider_t* p, void* ctx)` tears down driver hooks when `CONTRACT_PROVIDER_OP_REMOVE` runs.
- `ops` is an optional table of `sys_data_provider_op_t` rows (`0xFFxx` via `SYS_DATA_PROVIDER_OWN_OP(n)`, plain `cmd_f` handlers taking `ctx`); rows marked `SYS_DATA_PROVIDER_OP_LOCAL` are C-only and rejected with `ERR_PROV_LOCAL_OP` when routed.
- `sys_data_provider_t` carries no `.destroy`, and there is no static `add()` bypass.

## Control plane and router (`MODULE_ID_PROVIDER = 0x0D`)

- Wire header `sys_data_provider_router_hdr_t` (4 B packed LE): `[dir u8][id u8][op u16][args...]`, mirroring `sys_device_router`.
- Contract `CONTRACT_PROVIDER` (`0x0Dxx`):
  - `0x0D00` (`OP_CREATE`, W): `[type u8][flags u8][cfg]` (`SYS_DATA_PROVIDER_CREATE_SYSTEM` sets `STATE_SYSTEM`)
  - `0x0D01` (`OP_REMOVE`, W): fails `ERR_PROV_SYSTEM` / `ERR_PROV_IN_USE` if open or sending, then calls `type->remove(p, ctx)` and frees `ctx` when `ctx_size > 0`
  - `0x0D02` (`OP_LIST`, R on `ID_NONE` `0xFF`): returns `[u8 count][sys_data_provider_entry_t...]` (`provider u8, type u8, state u8`, 3 B each)
  - `0x0D03` (`OP_SEND`, W, `TAIL`): sends raw payload bytes directly
  - `0x0D04` (`OP_INFO`, R): returns `sys_data_provider_info_t` (`type u8, state u8`, 2 B)
  - `0x0D05` (`OP_OPS`, R): returns `[op u16][sides u8]` rows for registry ops and non-`LOCAL` driver ops
- `router/contract_provider.json` describes the wire commands (`router_key` and `contract_id` both 13: a descriptor with the two is addressed with the id and the 16-bit op, as the app reads it, templates/AGENTS.md); `errors/` owns `ERR_MOD_SYS_DATA_PROVIDER` (`MODULE_ID_PROVIDER`, tags `1..9`, `11..15`).

## Headers

`sys_data_provider.h` (the types, the state bits, the contract's ops and rows, the control calls, `sys_data_provider_create_system` (CREATE of the board's own provider: it builds the frame), `open` / `close` / `send` / `state`), `driver/sys_data_provider_driver.h` (for a driver only), `router/sys_data_provider_router.h`, `errors/sys_data_provider_errors.h`; `internal/` is the sources' own.

## Who owns the rx hook

A provider carries bytes and knows nothing of what they are. The one owner of its rx hook is whoever the data matrix's role (`data_matrix`, `DATA_MATRIX.MD`) made it: a session (`sys_session`), a raw port (its bytes go along the routes) or a command port (each message in is one command for the router). `open` refuses a second owner with `ERR_PROV_IN_USE`, and a provider with an owner cannot be removed: clear its role first.
