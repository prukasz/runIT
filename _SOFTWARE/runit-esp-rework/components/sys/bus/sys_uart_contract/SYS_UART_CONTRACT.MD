# sys_uart_contract — the UART contract and its client

Component `components/sys/bus/sys_uart_contract/`. A UART port is a device: the ESP32-S3's own UARTs are `device_esp32s3_uart` (the link to another board on UART1, a port on UART2), and a UART bridge on I2C or SPI is a device of its own. A project creates as many as it needs, each with its device ID. This component holds the **contract** every port implements (`UART`, `0x13`), its shared types/limits/errors, and the **client** a driver uses to reach its port. It makes no port. The console is not a device: it is `sys_uart`'s, made before the registry (SYS_UART.MD).

The contract claims `MODULE_ID_UART` (`0x13`, `ERR_MOD_SYS_UART`) via `SYS_DEVICE_CONTRACT_DEFINE` and `ERR_MODULE_DEFINE` in `sys_uart_contract.c` (linked `WHOLE_ARCHIVE`), declares `sys_uart_mode_e`, `sys_uart_parity_e`, `SYS_UART_WAIT_MAX_MS`, `SYS_UART_BAUD_MIN..MAX` directly in `sys_uart_contract.h`, and owns `errors/`. `sys_uart` depends on `sys_uart_contract` so pure contract consumers (device drivers) never pull ESP-IDF UART/USB drivers into their build.

## Files

| File | Role |
|---|---|
| `sys_uart_contract.h` | The contract table (`CONTRACT_UART_OPERATIONS`), `UART_EVENTS`, `sys_uart_mode_e`, `sys_uart_parity_e`, wait/baud limits (`//#ref-enum` / `//#ref-defines`), frames, and `sys_uart_port_subscribe` |
| `sys_uart_contract.c` | Claims `MODULE_ID_UART` / `ERR_MOD_SYS_UART`, implements `sys_uart_port_subscribe` |
| `errors/` | `ERR_MOD_SYS_UART` (`MODULE_ID_UART` 0x13), `ERR_SYS_UART_MAP`, error texts JSON |
| `CMakeLists.txt` | Requires `errors`, `cmd`, `sys_device`, `sys_event`; `WHOLE_ARCHIVE` |
| `AGENTS.md` | Rules for changing this folder |

## Control and readers

The contract stays transport-agnostic: a port is reached strictly by its **`device_id`** (never a SoC `uart_num`, which belongs only in `device_esp32s3_uart`'s `CREATE` struct, and never a `sys_data_provider` ID — `device_*` and `sys_uart_contract` stay completely isolated from `sys_data_provider`). Any port can be bridged to a `sys_data_provider` from outside via a provider adapter attaching through `RX_HOOK` and `SEND_FRAME`. A port has **one reader**:

| Reader | How | Gets each message |
|---|---|---|
| a device | `SUBSCRIBE` (`sys_uart_port_subscribe` in its `create()`) | in its own op, through `sys_device_deliver`, on the port's RX task |
| an external C consumer / adapter | `RX_HOOK` (`SYS_DEVICE_SET` / `SYS_DEVICE_WRITE`, `LOCAL`) | in `cb(ctx, data, len)`, on the port's RX task |
| nobody | — | in the port's buffer, for `RECEIVE` / `TRANSACT` |

Access rules:
- **`RECEIVE`, `FLUSH`, `SUBSCRIBE`, `TRANSACT`, and binding `RX_HOOK` (`cb != NULL`)**: refused (`ERR_UART_IN_USE{port, owner}`) whenever the port has a reader (`owner` is the subscriber's device ID, or `SYS_DEVICE_ID_NONE` for `RX_HOOK`). Unsubscribing (`SUBSCRIBE` with `op` 0) requires `caller == subscriber`; unbinding (`RX_HOOK` with `cb == NULL`) requires matching `ctx` (also `ERR_UART_IN_USE` otherwise).
- **`SEND` and `SEND_FRAME`**: allowed when nobody reads the port, when `caller == subscriber`, when `RX_HOOK` has `allow_send` (`RAW` / `RAW_LINE`, e.g. `IN` stream), or when `SEND_FRAME` carries `token == hook.ctx` (the bound adapter sending on `FRAMED`). Refused (`ERR_UART_IN_USE{port, owner}`) for non-subscriber callers while subscribed, and for unauthenticated `SEND` when `RX_HOOK` has `allow_send` false (never mix raw bytes with COBS frames); while in `DIRECT` mode, sends fail with `ERR_PROV_DOWN`.

A **message** is a `FRAMED` port's frame (its CRC checked), a `RAW` port's bytes up to a pause in the line, a `RAW_LINE` port's line up to its line feed; at most `CMD_FRAME_MAX` bytes locally (`CMD_DATA_MAX` via remote commands).

**Direct mode** (exact hardware timing, e.g. `sys_flasher` on the board's link) stays outside the contract in `sys_uart` (`sys_uart_link_direct`, `sys_data_provider_open`, `sys_uart_write`, `sys_uart_read`), because `sys_*` modules have no device caller ID and direct byte/SLIP transfers should not route through `sys_device_call` or remote bridges.

**Delivery to a subscriber** checks the generation the port took at `SUBSCRIBE`: a subscriber removed, or another device under its ID, is skipped, never called. The subscriber's op is a write of its own (`SYS_DEVICE_OWN_OP`), `LOCAL | TAIL | ALWAYS`, its value `sys_uart_rx_t` `{u8 port}` and the message as its tail; best `NOLOCK` too, copying the message and returning: `sys_device_deliver` waits for a locked op's lock, and the port's RX task waits with it.

## The contract

| Op | Code | Sides | Flags | Frame | Does |
|---|---|---|---|---|---|
| `INFO` | `0x00` | R | | → `sys_uart_info_t` `{u8 mode, u32 baud, u8 parity, u8 stop_bits}` | the port |
| `BAUD` | `0x01` | R, W | ALWAYS | `u32` | its rate (`SYS_UART_BAUD_MIN..MAX`) |
| `SEND` | `0x02` | W | ALWAYS, TAIL, NOLOCK | `sys_uart_send_t` `{u16 wait_ms, u8 flags}` + bytes | onto the line: `RAW` as-is, `FRAMED` as one frame; `flags`: 0 normal or `CMD_SEND_*` (`URGENT`, `SPARE`, `NOWAIT`); waits up to `wait_ms` (`0` / `NOWAIT`: immediate), else `ERR_UART_BUSY` |
| `RECEIVE` | `0x03` | R | NOLOCK | key `sys_uart_receive_t` `{u16 wait_ms}` → raw message bytes | the next message, waiting at most `wait_ms`: returns as soon as one is whole (`ERR_CMD_OUT_FULL` if > `out->cap`); empty when none came |
| `FLUSH` | `0x04` | W | ALWAYS | – | the buffer emptied |
| `SUBSCRIBE` | `0x05` | W | ALWAYS, LOCAL | `sys_uart_subscribe_t` `{u16 op}` | the caller the reader, each message to its op; `op` 0 lets it go |
| `TRANSACT` | `0x06` | W | ALWAYS, TAIL, NOLOCK | `sys_uart_transact_t` `{u16 wait_ms}` + TX bytes → raw reply bytes | atomic flush + send + wait for reply in one op (`ERR_CMD_OUT_FULL` if > `out->cap`) |
| `SEND_FRAME` | `0x07` | W | ALWAYS, LOCAL, NOLOCK | `sys_uart_send_frame_t` `{frame, wait_ms, flags, token}` | flat frame up to `CMD_FRAME_MAX` (`cmd_in_t`) with `CMD_SEND_*` (`URGENT`, `SPARE`, `NOWAIT`) flags and `token` |
| `RX_HOOK` | `0x08` | W | ALWAYS, LOCAL | `sys_uart_rx_hook_t` `{cb, ctx, allow_send}` | binds or releases non-device RX callback (`data == NULL` reports up/down `len` 1/0) |

- **Every write op is ALWAYS (and reads always run)**: a port has no outputs to make safe, so a suspended port (the safe state's sweep) keeps carrying the link's frames, `SAFE_LEAVE` among them, and its drivers' suspend hooks. A port's suspend and resume do nothing.
- **`SEND`, `SEND_FRAME`, `RECEIVE`, `TRANSACT` are NOLOCK**: waiting up to `SYS_UART_WAIT_MAX_MS` (`1000` ms > `CONFIG_SYS_DEVICE_LOCK_TIMEOUT_MS`) never holds the device slot lock, so TX/RX/INFO from another task do not contend on the registry lock.
- **`SUBSCRIBE`, `SEND_FRAME`, `RX_HOOK` are LOCAL**: only firmware C reaches them, never a command or a VM program.
- **Event `UART_EV_OVERFLOW` (`0x01`, `SYS_EVENT_NOTIFY`, `{uint16_t dropped}`)**: published under `MODULE_ID_UART` (`source = port device ID`, `channel = 0`) on real RX buffer/FIFO/queue overflow (rate-throttled); corrupted `FRAMED` candidates are silently ignored.

## Client calls in C

Like the other contracts (`sys_hbridge`, `sys_power`), `sys_uart_contract` provides no redundant one-line wrappers or intermediate stack TX buffers:

- **`sys_uart_port_subscribe(owner, &sub, port, op)`**: the one client helper in `sys_uart_contract.c` (matching `sys_i2c_dev_attach`) — declares the `OP_SUBSCRIBE` dependency, registers `sub` as a resource of `owner` so `REMOVE` automatically unsubscribes, and runs `OP_SUBSCRIBE`. A non-subscribing driver calls `sys_device_use(owner, port)` in its `create()`.
- **`INFO`, `BAUD`, `FLUSH`, `SEND_FRAME`, `RX_HOOK`**: called directly via `SYS_DEVICE_GET` / `SYS_DEVICE_SET` / `SYS_DEVICE_WRITE` (zero stack buffers; `OP_SEND_FRAME` is the zero-copy TX path for firmware C).
- **`RECEIVE` (and wire `SEND` / `TRANSACT`)**: variable-length `cmd_out_t` / `TAIL` ops called directly via `sys_device_call` (or by router/CLI commands).

## Errors

`ERR_MOD_SYS_UART` (`MODULE_ID_UART` 0x13): `ERR_UART_BAD_ARG`, `_BAD_PORT{port}` (sys_uart link lent without one), `_BUSY{port}` (TX queue full or `SPARE` past half), `_BAD_BAUD{baud}`, `_INIT{port}`, `_NOT_DIRECT{provider}`, `_TIMEOUT{provider}`, `_IN_USE{port, owner}` (reader active, non-owner unsubscribe/unhook, or unauthenticated SEND on FRAMED). When a port is bridged to `sys_data_provider`, the adapter translates `ERR_UART_BUSY` / `ERR_DEV_SUSPENDED` to `ERR_PROV_BUSY` / `_DOWN`. A missing port is the registry's `ERR_DEV_NOT_FOUND`, a port still in use its `ERR_DEV_IN_USE`.

## Open

- `device_esp32s3_uart` implements `CONTRACT_UART` (`0x004D`) with `uart_num` in its `CREATE` config and zero `sys_data_provider` coupling; `components/providers/provider_uart/` self-registers `SYS_DATA_PROVIDER_TYPE_UART` (`1`) to bridge any chosen `CONTRACT_UART` device to `sys_data_provider` via `OP_RX_HOOK` + `OP_SEND_FRAME`, and `main.c` creates the board's `ESP32S3_GPIO` (`0x00`) and link `ESP32S3_UART` (`0x01`) (`SYS_DEVICE_CREATE_SYSTEM`) plus link provider `0x02` (`SYS_DATA_PROVIDER_CREATE_SYSTEM` via `CONTRACT_PROVIDER_OP_CREATE`).
- Stage 6 (`sys_flasher`) will add `DIRECT` flasher mode handover on top of the device-backed link.
- The descriptor `contract_uart.json` (the ops for the app) is not written yet, and `errors.json` still references legacy `runit-esp` paths; `templates/sync.sh` has not been run for it.
