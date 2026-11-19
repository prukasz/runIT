# sys_data_provider — typed provider registry and router

Component `components/sys/sys_data_provider/`. Keeps the board's ways bytes leave and enter — UART ports, BLE links, MQTT topics, board console — under 1-byte `id` (`0..0xFE`; `SYS_DATA_PROVIDER_ID_NONE = 0xFF` for `LIST`), each one a stream port of the data matrix (its whole data plane, "Stream port"), controlled by commands only (`sys_data_provider_router` under `MODULE_ID_PROVIDER = 0x0D`). In C there is only `sys_data_provider_create_system`, the boot's CREATE of the board's own providers.

## Driver model

The registry keeps the providers' lifecycle and nothing of their logic. Every provider is created via `CREATE` from a driver registered under `sys_data_provider_type_e` (`u8`: `UART = 1`, `BLE = 2`, `MQTT = 3`, `CONSOLE = 4`) using `SYS_DATA_PROVIDER_TYPE_DEFINE(prov_type, cfg_t, prov_t, create_fn, remove_fn, in_fn, ops_ptr, ops_cnt)`:
- `prov_t` is the driver's own struct; its first field is the base `sys_data_provider_t base`: the port (`port`) and the state bits (`state`), nothing else (the id is the port's low byte, the type is the registry's). It lives in the registry's slot (`CONFIG_SYS_DATA_PROVIDER_SIZE` bytes, 64 by default; a larger struct fails to build), never freed: the registry zeroes it at CREATE (all but the port's input), names its port and sets its state (`STATE_SYSTEM`).
- `create(sys_data_provider_t* p, const void* cfg)` makes the instance (its line) and sets capability flags on `p->state` (`WIRED`, `FRAMED`, initial `UP`), using `sys_data_provider_deliver()` / `sys_data_provider_set_up()` for RX.
- `remove(sys_data_provider_t* p)` tears down the driver's hooks when `CONTRACT_PROVIDER_OP_REMOVE` runs. It can't fail: when it returns, no RX callback of the driver runs any more. What the driver allocated in `create` (queues, buffers, a client) it frees here; its struct is the registry's.
- `in` is the port's input (`data_matrix_in_f`): the packet onto the line (in it the driver gets its struct with `SYS_DATA_PROVIDER_OF(self, prov_t)`).
- `ops` is an optional table of `sys_data_provider_op_t` rows (`0xFFxx` via `SYS_DATA_PROVIDER_OWN_OP(n)`, plain `cmd_f` handlers taking the provider as `ctx`); they are reached by the router only.
- **One call into a driver at a time:** its `in`, its own ops and its `remove` run under the provider's own mutex (the registry's, per slot), so a driver keeps no lock of its own for them. `sys_data_provider_deliver` and `sys_data_provider_set_up` come from the driver and take none.

## Control plane and router (`MODULE_ID_PROVIDER = 0x0D`)

- Wire header `sys_data_provider_router_hdr_t` (4 B packed LE): `[dir u8][id u8][op u16][args...]`, mirroring `sys_device_router`.
- Contract `CONTRACT_PROVIDER` (`0x0Dxx`):
  - `0x0D00` (`OP_CREATE`, W): `[type u8][flags u8][cfg]` (`SYS_DATA_PROVIDER_CREATE_SYSTEM` sets `STATE_SYSTEM`)
  - `0x0D01` (`OP_REMOVE`, W): fails `ERR_PROV_SYSTEM`, unregisters its port from the data matrix first (`data_matrix_unregister`: its links go), then waits for the call into its driver in flight and calls `type->remove(p)`; the slot is free again
  - `0x0D02` (`OP_LIST`, R on `ID_NONE` `0xFF`): returns `[u8 count][sys_data_provider_entry_t...]` (`provider u8, type u8, state u8`, 3 B each)
  - `0x0D05` (`OP_OPS`, R): returns `[op u16][sides u8]` rows for registry ops and the driver's own ops
- `router/contract_provider.json` describes the wire commands (`router_key` and `contract_id` both 13: a descriptor with the two is addressed with the id and the 16-bit op, as the app reads it, templates/AGENTS.md); `errors/` owns `MODULE_ID_PROVIDER` (`MODULE_ID_PROVIDER`, tags `1..5`, `11..13`, `15`).

## Headers

`sys_data_provider.h` (the types, the state bits, the contract's ops and rows, `sys_data_provider_create_system`: CREATE of the board's own provider), `driver/sys_data_provider_driver.h` (for a driver only), `router/sys_data_provider_router.h`, `errors/sys_data_provider_errors.h`; `internal/` is the sources' own.

## Stream port

A provider carries bytes and knows nothing of what they are. It is a stream port (`data_matrix.h`, DATA_MATRIX.MD), and nothing else on its data plane: `sys_data_provider_t.port`, embedded in it, named `DATA_MATRIX_PORT(DATA_MATRIX_PORT_PROVIDER, id)`. CREATE registers it in the data matrix (`data_matrix_register`; no room there: the provider is not made, its driver removed again), REMOVE unregisters it first (its links go before its driver does). Its input is the registry's: it hands the packet, as it is and with its flags, to the type's `in` under the provider's mutex (one call into the driver at a time); the registry never looks into a packet. The driver puts it onto the line (`CMD_SEND_NOWAIT`: no room, dropped at once; without it the driver waits for room up to its limit, `CONFIG_PROVIDER_*_SEND_WAIT_MS`, on the sender's task; what the line does not take is lost, no error). A packet that waited for the mutex while the provider was removed is dropped. The data matrix does not wait for its sends in flight (DATA_MATRIX.MD): one whose targets were copied before REMOVE still calls the port, which is why the driver's struct lives in the registry's slot and is never freed; the port's input stays the registry's and drops the packet. A slot CREATEd again in the meantime takes it as its own. The data matrix knows nothing of a provider's state: a removed provider is simply out of it. What comes in on the line (`sys_data_provider_deliver`, from the driver's task) goes out of its port to the ports linked from it, with no flags. `sys_data_provider_set_up` only sets `SYS_DATA_PROVIDER_STATE_UP`.

## Events

`events/sys_data_provider_events.h`: `SYS_DATA_PROVIDER_EVENTS` under `MODULE_ID_PROVIDER` (`0x0D`), lane `SYS`, source the provider's id, channel 0. What is not bytes of a provider goes here, never through the data matrix.

| Event | Code | Flags | Payload | When |
|---|---|---|---|---|
| `CREATED` | `0x01` | | `{type u8, state u8}` | CREATE done, its port in the data matrix |
| `REMOVED` | `0x02` | | `{type u8}` | REMOVE done: port out, driver removed, id free |
| `UP` | `0x03` | `NOTIFY` | | its line delivers frames now (`STATE_UP` set) |
| `DOWN` | `0x04` | `NOTIFY` | | its line delivers none any more (`STATE_UP` cleared) |

Defined only: the registry does not publish them yet (CREATE, REMOVE and `sys_data_provider_set_up` will).

## Concurrency

- `in`, the own ops and `remove` of one provider run one at a time (its slot's mutex); CREATE, REMOVE and the own ops of all providers one at a time (the registry's control mutex), always taken before a slot's.
- `in` and the own ops never CREATE or REMOVE a provider: they hold those locks, it would deadlock.
- `in` waits for room on its line a bounded time only (`CONFIG_PROVIDER_*_SEND_WAIT_MS`): REMOVE and the provider's own ops wait for it.
- `sys_data_provider_deliver` and `sys_data_provider_set_up` come from the driver's RX side, at the same time as `in`, and take no lock of the registry (a provider's RX goes on while its TX waits, and two providers linked both ways never wait for each other): what RX and TX share, the driver guards itself.
- A packet that was on its way when the provider was removed (in the data matrix, or an RX callback in flight) may reach a provider CREATEd again in the same slot meanwhile: it is taken as that provider's. The window is a sender preempted across a REMOVE and a CREATE.

## Open

- `host/test_sys_data_provider.c` tests an older interface (`send`, `open`, `spare_refused`); it is rewritten with the drivers.
