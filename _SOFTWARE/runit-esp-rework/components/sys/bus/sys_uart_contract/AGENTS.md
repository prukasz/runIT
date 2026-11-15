# sys_uart_contract

Spec: `SYS_UART_CONTRACT.MD` (this folder).

- Every change in this folder is followed by an edit of `SYS_UART_CONTRACT.MD`, if it is needed.
- This component owns the contract, its shared types/enums (`sys_uart_contract.h`), its events (`UART_EVENTS`), its error definitions (`errors/`), `SYS_DEVICE_CONTRACT_DEFINE(UART, 0x13)`, and the subscription helper (`sys_uart_port_subscribe`; plain ops use `SYS_DEVICE_SET` / `GET` / `WRITE` or `sys_device_call` directly without wrappers or stack buffers). A port is a device (`device_esp32s3_uart`, bridges later); nothing here includes a port's implementation, and a driver on a port depends on `sys_uart_contract` only (never `sys_uart`).
- `CMakeLists.txt` must keep `WHOLE_ARCHIVE` so `ERR_MODULE_DEFINE(ERR_MOD_SYS_UART, ...)` and `SYS_DEVICE_CONTRACT_DEFINE(UART, 0x13)` are never stripped by the linker when no client function is called directly.
- `CONTRACT_UART_OPERATIONS` is the one table of the ops; the op numbers, frames and flags are wire format for the remote ops (`INFO`, `BAUD`, `SEND`, `RECEIVE`, `FLUSH`, `TRANSACT`). `SUBSCRIBE`, `SEND_FRAME` and `RX_HOOK` stay `LOCAL`. `DIRECT` mode (`sys_flasher` on the board link) stays in `sys_uart`, never as contract ops here.
- Never renumber error tags in `errors/sys_uart_errors.h` or enum values in `sys_uart_contract.h`.
- Every write op stays `ALWAYS`: the safe state suspends every device and the link must carry `SAFE_LEAVE`. Blocking waits (`SEND`, `SEND_FRAME`, `RECEIVE`, `TRANSACT`) stay `NOLOCK`.
- Both the contract and UART drivers stay completely isolated from `sys_data_provider` (and vice versa): ports are addressed by `device_id` only (`uart_num` belongs in `device_esp32s3_uart`'s `CREATE` config), while an external provider adapter binds via `OP_RX_HOOK` + `OP_SEND_FRAME` (`token == ctx`). A device reads a port by `SUBSCRIBE` (delivery to its own op, checked by generation), never via a callback pointer.
- `SUBSCRIBE` takes `sys_device_caller()` as owner (`create()`); `SEND` / `SEND_FRAME` are allowed for nobody-reading, `caller == subscriber`, `RX_HOOK` with `allow_send` (`RAW` / `RAW_LINE`), or `SEND_FRAME` with matching `token` (`FRAMED`), and refused (`ERR_UART_IN_USE`) for non-subscribers or unauthenticated sends when `RX_HOOK` has `allow_send` false.
