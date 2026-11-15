# sys_uart_contract

Spec: `SYS_UART_CONTRACT.MD` (this folder).

- Every change in this folder is followed by an edit of `SYS_UART_CONTRACT.MD`, if it is needed.
- This component owns the contract (`sys_uart_contract.h`: table, events, modes, parity, limits), its errors (`errors/`), `SYS_DEVICE_CONTRACT_DEFINE(UART, 0x13)`, the subscription helper (`sys_uart_port_subscribe`; plain ops go through `SYS_DEVICE_SET` / `GET` / `WRITE` or `sys_device_call`, no wrappers), and the line (`sys_uart_codec.h`: framing and the TX queue, the one copy for the ports and the console).
- Layers: it depends on `core` and `sys_device` / `sys_event` only. It knows no port's implementation and no `sys_data_provider`; a port knows only this contract; a provider bridges a port from outside (`provider_uart`). `provider_console` uses `sys_uart_codec.h`, never a port.
- `CMakeLists.txt` keeps `WHOLE_ARCHIVE`, so `ERR_MODULE_DEFINE(ERR_MOD_SYS_UART, ...)` and `SYS_DEVICE_CONTRACT_DEFINE(UART, 0x13)` are never stripped.
- `CONTRACT_UART_OPERATIONS` is the one table; op numbers, frames and flags of the remote ops (`INFO`, `BAUD`, `SEND`, `RECEIVE`, `FLUSH`, `TRANSACT`) are wire format. `SUBSCRIBE`, `SEND_FRAME`, `RX_HOOK` stay `LOCAL`. Never renumber error tags or enum values.
- Every write op stays `ALWAYS`; the waiting ones (`SEND`, `SEND_FRAME`, `RECEIVE`, `TRANSACT`) stay `NOLOCK`.
- One reader at a time (subscriber, hook or a read under way), and the access rules of `SYS_UART_CONTRACT.MD` "Readers": no tokens, no per-reader send permissions, no up notification (a port is up while it exists).
