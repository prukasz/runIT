# sys_uart_contract — the UART contract, its client and the line

Component `components/sys/bus/sys_uart_contract/`. A UART port is a device: the ESP32-S3's own controllers are `device_esp32s3_uart`, a UART bridge on I2C or SPI is a device of its own. The project creates as many as it needs, with any controller, mode, rate and pins, each under its device ID. A port does three jobs: the **link** between boards (a session over `provider_uart`), the **way to a device** (a driver that subscribes), a **raw port** the app or a stream reads. The board's **console** (UART0 + USB-Serial-JTAG) is not a port but a provider of its own (`provider_console`), sharing the line's framing and TX queue from here.

This component holds the **contract** every port implements (`UART`, `0x13`), the **client** a driver uses to subscribe, and the **line** (`sys_uart_codec.h`): the FRAMED line's framing and a port's TX queue. It makes no port and knows no provider: a port is reached by its device ID only, a provider bridges it from outside (`provider_uart`).

## Files

| File | Role |
|---|---|
| `sys_uart_contract.h` | The contract table (`CONTRACT_UART_OPERATIONS`), `UART_EVENTS`, `sys_uart_mode_e`, `sys_uart_parity_e`, the wait and baud limits, the frames, `sys_uart_port_subscribe` |
| `sys_uart_contract.c` | Registers the errors of `MODULE_ID_UART` (`ERR_MODULE_DEFINE`), implements `sys_uart_port_subscribe` |
| `sys_uart_codec.h` / `.c` | `sys_uart_unframe` (a FRAMED candidate decoded, its CRC checked); the TX queue `sys_uart_txq_*` (urgent and normal ring, SPARE, framing on the way out) |
| `errors/` | `MODULE_ID_UART` (0x13), `ERR_SYS_UART_MAP`, the messages |
| `CMakeLists.txt` | Requires `errors`, `cmd`, `sys_device`, `sys_event`, `esp_ringbuf`; `WHOLE_ARCHIVE` |

## The line

- **FRAMED**: `00 cobs([frame][crc16 LE]) 00`, CRC16-CCITT (poly `0x1021`, init `0xFFFF`, no reflection, no final xor). On the way in, the bytes up to each `0x00` are a candidate; `sys_uart_unframe` decodes it in place and checks its CRC; anything else (boot text, noise, a frame cut by an overflow) is dropped without a word. **RAW** / **RAW_LINE**: the bytes as they are.
- **The TX queue** (`sys_uart_txq_t`): a send copies the frame in once (with its CRC on a FRAMED line) and wakes the writer; `CMD_SEND_URGENT` goes to a ring of its own (two whole frames) written first, `CMD_SEND_SPARE` is refused past half of its ring, `CMD_SEND_NOWAIT` never waits. The writer's task takes the next frame as it goes on the line (`sys_uart_txq_pop`, in `q->line`).

## Readers

A port has **one reader** at a time:

| Reader | How | Gets each message |
|---|---|---|
| a device | `SUBSCRIBE` (`sys_uart_port_subscribe` in its `create()`) | in its own op, `sys_device_deliver` (checked by generation), on the port's RX task |
| a C consumer (a provider adapter: a way to another board) | `RX_HOOK` `{cb, ctx}` | in `cb(ctx, data, len)`, on the port's RX task under the port's lock (what it waits for, the port's RX waits for; once `RX_HOOK` lets go of it, it is never called again); `data` NULL once, when the port is removed |
| a `RECEIVE` / `TRANSACT` under way | the call | the next message of the port's queue |

With no reader the messages wait in the port's queue for `RECEIVE`; when it is full, the oldest goes.

- `RECEIVE`, `FLUSH`, `TRANSACT`, `SUBSCRIBE` and `RX_HOOK` are refused (`ERR_UART_IN_USE{port, owner}`, `owner` the subscriber or `SYS_DEVICE_ID_NONE`) while another reader holds the port. A reader lets go only of what it holds (`SUBSCRIBE` with `op` 0 by its subscriber, `RX_HOOK` with `cb` NULL and its `ctx`); anything else is no change.
- `SEND` is refused on a port a subscriber holds, but for the subscriber, and on a `FRAMED` port a hook holds: raw bytes would break its frames. A `RAW` port a stream reads takes `SEND` from anyone.
- `SEND_FRAME` is C's (`LOCAL`): never refused.

A **message** is a `FRAMED` port's frame, a `RAW` port's bytes up to a pause in the line, a `RAW_LINE` port's line up to its line feed (`\r` before it dropped); a `RAW` or `RAW_LINE` message is at most `CMD_FRAME_MAX` bytes (`CMD_DATA_MAX` via remote commands).

## The contract

| Op | Code | Sides | Flags | Frame | Does |
|---|---|---|---|---|---|
| `INFO` | `0x00` | R | | → `sys_uart_info_t` `{u8 mode, u32 baud, u8 parity, u8 stop_bits}` | the port |
| `BAUD` | `0x01` | R, W | ALWAYS | `u32` | its rate (`SYS_UART_BAUD_MIN..MAX`) |
| `SEND` | `0x02` | W | ALWAYS, TAIL, NOLOCK | `sys_uart_send_t` `{u16 wait_ms, u8 flags}` + bytes | onto the line, `FRAMED` as one frame; `CMD_SEND_*`; no room in `wait_ms`: `ERR_UART_BUSY` |
| `RECEIVE` | `0x03` | R | NOLOCK | key `sys_uart_receive_t` `{u16 wait_ms}` → the message | the next message, at most `wait_ms`; empty when none came |
| `FLUSH` | `0x04` | W | ALWAYS | – | what came in dropped |
| `SUBSCRIBE` | `0x05` | W | ALWAYS, LOCAL | `sys_uart_subscribe_t` `{u16 op}` | the caller the reader, each message to its op; `op` 0 lets go |
| `TRANSACT` | `0x06` | W | ALWAYS, TAIL, NOLOCK | `sys_uart_transact_t` `{u16 wait_ms}` + bytes → the reply | flush, send, the next message, in `wait_ms` together |
| `SEND_FRAME` | `0x07` | W | ALWAYS, LOCAL, NOLOCK | `sys_uart_send_frame_t` `{frame, wait_ms, flags}` | one flat frame (`cmd_in_t`, 1..`CMD_FRAME_MAX`) into the queue |
| `RX_HOOK` | `0x08` | W | ALWAYS, LOCAL | `sys_uart_rx_hook_t` `{cb, ctx}` | binds a C reader; `cb` NULL lets go |

- **Every write op is ALWAYS**: a port has no outputs to make safe, so a suspended port (the safe state's sweep) keeps carrying a session's frames, `SAFE_LEAVE` among them. A port's suspend and resume do nothing.
- **`SEND`, `SEND_FRAME`, `RECEIVE`, `TRANSACT` are NOLOCK**: their waits (at most `SYS_UART_WAIT_MAX_MS`) never hold the device's lock.
- **Event `UART_EV_OVERFLOW`** (`0x01`, `SYS_EVENT_NOTIFY`, `{u16 dropped}`, source the port): one per overflow of the line's RX FIFO or driver buffer (`dropped` 1). A candidate failing its CRC is no event.

## Client calls in C

- **`sys_uart_port_subscribe(owner, &sub, port, op)`**: declares the use of `OP_SUBSCRIBE`, registers `sub` as a resource of `owner` (its `REMOVE` unsubscribes), and subscribes. A driver that only sends calls `sys_device_use(owner, port)` in its `create()`.
- The rest directly: `SYS_DEVICE_GET` / `SET` / `WRITE` (`INFO`, `BAUD`, `FLUSH`, `SEND_FRAME`, `RX_HOOK`), `sys_device_call` for the variable-length ones (`RECEIVE`, `SEND`, `TRANSACT`).

## Errors

`MODULE_ID_UART` (0x13): `ERR_UART_BAD_ARG`, `_BUSY{port}` (no room in the TX queue, or `SPARE` past half), `_BAD_BAUD{baud}`, `_INIT{port}`, `_IN_USE{port, owner}`. `provider_uart` drops a frame the port does not take. A missing port is the registry's `ERR_DEV_NOT_FOUND`, a port still in use its `ERR_DEV_IN_USE`.

## Open

- The descriptor `contract_uart.json` (the ops for the app) is not written yet.
