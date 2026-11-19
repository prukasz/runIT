# sys_data_provider — the providers' lifecycle

Component `components/sys/sys_data_provider/`. Keeps the board's ways bytes leave and enter — UART ports, BLE links, MQTT topics, the board's console — under a 1-byte `id`, each one a stream port of the data matrix (its whole data plane, "Stream port"), controlled by commands only (`sys_data_provider_router` under `MODULE_ID_PROVIDER = 0x0D`). The registry keeps the providers' lifecycle and nothing of their logic: it never looks into a packet. In C there is only `sys_data_provider_create_system`, the boot's CREATE of the board's own providers.

## Files

| File | Role |
|---|---|
| `sys_data_provider.h` | Public: the driver types (`sys_data_provider_type_e`), the state bits (`SYS_DATA_PROVIDER_STATE_*`), `sys_data_provider_create_system` |
| `driver/sys_data_provider_driver.h` | For a driver only: the base `sys_data_provider_t`, the type (`SYS_DATA_PROVIDER_TYPE_DEFINE`), the own ops (`SYS_DATA_PROVIDER_OWN_OP`), `sys_data_provider_set_up`, the driver's side of the contract |
| `sys_data_provider.c` | The registry: the types, the slots, the locks, CREATE / REMOVE, the port's input, the own ops' call; no wire format |
| `internal/sys_data_provider_internal.h` | The seam between the registry and its router: create, remove, list, an own op |
| `router/sys_data_provider_router.c` / `.h`, `router/contract_provider.json` | The router: the wire format (op codes, the CREATE header, the LIST row, the frame header), its commands |
| `errors/` | `ERR_SYS_DATA_PROVIDER_MAP` (`MODULE_ID_PROVIDER`, tags `1..5`, `12`, `13`, `15`), its messages |
| `events/sys_data_provider_events.h` | The providers' events (defined, not published yet) |

## Driver model

Every provider is created via `CREATE` from a driver registered under `sys_data_provider_type_e` (`u8`: `UART = 1`, `BLE = 2`, `MQTT = 3`, `CONSOLE = 4`) using `SYS_DATA_PROVIDER_TYPE_DEFINE(prov_type, cfg_t, prov_t, create_fn, remove_fn, in_fn, ops_ptr, ops_cnt)`:

- `prov_t` is the driver's own struct; its first field is the base `sys_data_provider_t base`: the port (`port`) and the state bits (`state`), nothing else (the id is the port's low byte, the type is the registry's). It lives in the registry's slot (`CONFIG_SYS_DATA_PROVIDER_SIZE` bytes, 64 by default; a larger struct fails to build), never freed: the registry zeroes it at CREATE (all but the port's input), names its port and sets its state (`STATE_SYSTEM`).
- `create(sys_data_provider_t* p, const void* cfg)` makes the instance (its line) and sets capability flags on `p->state` (`WIRED`, `FRAMED`, initial `UP`).
- `remove(sys_data_provider_t* p)` tears down the driver's hooks. It can't fail: when it returns, no RX callback of the driver runs any more. What the driver allocated in `create` (queues, buffers, a client) it frees here; its struct is the registry's.
- `in` is the port's input (`data_matrix_in_f`): the packet onto the line (in it the driver gets its struct with `SYS_DATA_PROVIDER_OF(self, prov_t)`).
- `ops` is an optional table of `sys_data_provider_op_t` rows (`SYS_DATA_PROVIDER_OWN_OP(n)`, `0xFFxx`; plain `cmd_f` handlers taking the provider as `ctx`), reached by the router only: what a type has beyond the contract (an MQTT topic, ...).
- What comes in on the line the driver sends out of its port itself (`data_matrix_send(&p->port, 0, data, len)`, from its task); its line's state with `sys_data_provider_set_up` (`STATE_UP` only).

## Commands (`MODULE_ID_PROVIDER = 0x0D`)

Frame `sys_data_provider_router_hdr_t` (4 B packed LE): `[dir u8][id u8][op u16][args...]`.

| Op | Code | Args / answer | Meaning |
|---|---|---|---|
| `CREATE` (W) | `0x0D00` | `[type u8][flags u8][cfg]` | makes provider `id` (flag bit 0: the board's own, `STATE_SYSTEM`); its port in the data matrix (no room there: it is not made) |
| `REMOVE` (W) | `0x0D01` | | not the board's own (`ERR_PROV_SYSTEM`): its port out of the data matrix first (its links go), then, after the call into its driver in flight, `remove`; the slot is free again |
| `LIST` (R) | `0x0D02` | `[u8 count][provider u8, type u8, state u8]...` | every provider; any `id` |
| own (R/W) | `0xFFxx` | the driver's | a driver's own op; the app knows a type's own ops from its descriptor |

`router/contract_provider.json` describes them (`router_key` and `contract_id` both 13: a descriptor with the two is addressed with the id and the 16-bit op, as the app reads it, templates/AGENTS.md).

## Stream port

A provider carries bytes and knows nothing of what they are. It is a stream port (`data_matrix.h`, DATA_MATRIX.MD), and nothing else on its data plane: `sys_data_provider_t.port`, named `DATA_MATRIX_PORT(DATA_MATRIX_PORT_PROVIDER, id)`. Its input is the registry's: it hands the packet, as it is and with its flags, to the type's `in` under the provider's mutex. The driver puts it onto the line (`CMD_SEND_NOWAIT`: no room, dropped at once; without it the driver waits for room up to its limit, `CONFIG_PROVIDER_*_SEND_WAIT_MS`, on the sender's task; what the line does not take is lost, no error). The data matrix knows nothing of a provider's state: a removed provider is simply out of it. It does not wait for its sends in flight either (DATA_MATRIX.MD): one whose targets were copied before REMOVE still calls the port, which is why the driver's struct lives in the registry's slot and is never freed; the port's input stays the registry's and drops the packet.

## Concurrency

- `in`, the own ops and `remove` of one provider run one at a time (its slot's mutex), so a driver keeps no lock of its own for them. CREATE, REMOVE and the own ops of all providers run one at a time (the registry's control mutex), always taken before a slot's.
- `in` and the own ops never CREATE or REMOVE a provider: they hold those locks, it would deadlock.
- `in` waits for room on its line a bounded time only (`CONFIG_PROVIDER_*_SEND_WAIT_MS`): REMOVE and the provider's own ops wait for it.
- The driver's RX side (`data_matrix_send` on its port, with the flags its driver gives: `CMD_SEND_NOWAIT` and its RX never waits for a taker, without it a taker may make it wait for room; `sys_data_provider_set_up`) runs at the same time as `in` and takes no lock of the registry (a provider's RX goes on while its TX waits, and two providers linked both ways never wait for each other): what RX and TX share, the driver guards itself.
- A packet that was on its way when the provider was removed (in the data matrix, or an RX callback in flight) may reach a provider CREATEd again in the same slot meanwhile: it is taken as that provider's. The window is a sender preempted across a REMOVE and a CREATE.

## Events

`events/sys_data_provider_events.h`: `SYS_DATA_PROVIDER_EVENTS` under `MODULE_ID_PROVIDER` (`0x0D`), lane `SYS`, source the provider's id, channel 0. What is not bytes of a provider goes here, never through the data matrix.

| Event | Code | Flags | Payload | When |
|---|---|---|---|---|
| `CREATED` | `0x01` | | `{type u8, state u8}` | CREATE done, its port in the data matrix |
| `REMOVED` | `0x02` | | `{type u8}` | REMOVE done: port out, driver removed, id free |
| `UP` | `0x03` | `NOTIFY` | | its line delivers frames now (`STATE_UP` set) |
| `DOWN` | `0x04` | `NOTIFY` | | its line delivers none any more (`STATE_UP` cleared) |

Defined only: the registry does not publish them yet (CREATE, REMOVE and `sys_data_provider_set_up` will).

## Config (`Kconfig`)

`SYS_DATA_PROVIDER_MAX` 8 (the slots), `SYS_DATA_PROVIDER_SIZE` 64 (the largest driver struct), `SYS_DATA_PROVIDER_TYPES` 8.

## Open

- `host/test_sys_data_provider.c` tests an older interface (`send`, `open`, `spare_refused`); it is rewritten with the drivers.
