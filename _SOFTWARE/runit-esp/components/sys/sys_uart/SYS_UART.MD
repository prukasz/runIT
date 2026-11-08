# sys_uart — UART ports as providers

Component `components/sys/sys_uart/`. Makes UART ports **providers** (SYS_DATA_PROVIDER.MD). Three kinds, all made by the same code:

| Port | Provider | Made by | Removable | Mode |
|---|---|---|---|---|
| **The console's** (GPIO 43/44 via the board's USB-UART bridge, 921600 baud (`CONFIG_ESP_CONSOLE_UART_BAUDRATE`, the link's rate), and the native USB-Serial-JTAG when it is a console output) | what the board gives `sys_uart_init` | `sys_uart_init`, always | no (`STATE_SYSTEM`) | framed |
| **The link to another board** (`CONFIG_SYS_UART_LINK`, on by default) | what the board gives `sys_uart_init` | `sys_uart_init`, from Kconfig | no (`STATE_SYSTEM`) | framed |
| **A port CREATE made** | the id it is given | the `UART` router (below) or `sys_uart_create` | yes: the provider's REMOVE | framed or raw |

The ESP32-S3 has three UARTs (0, 1, 2): the console takes `CONFIG_ESP_CONSOLE_UART_NUM` (0), the link `CONFIG_SYS_UART_LINK_PORT` (1), so a board with the link has UART 2 left for CREATE and one without has 1 and 2. **A UART is one provider and a provider has one reader** (a session, an IN stream's binding, or the command binding): two providers on one UART would share its RX bytes, each byte going to only one of them. Writing takes only its id (SYS_DATA_PROVIDER.MD "Use"), so an IN and an OUT stream can share a raw port. A line that must carry commands, every stream and the watches at once is **one framed port with one session** (SYS_SESSION.MD): its frames name the destination and the stream, so nothing is added to the wire; a device that talks its own protocol (a GPS) gets a UART of its own as a raw port.

## Files

| File | Role |
|---|---|
| `sys_uart.h` | API: `sys_uart_init`, `sys_uart_create`, `sys_uart_get`, `sys_uart_transact`; a DIRECT port's `sys_uart_write`, `sys_uart_read`, `sys_uart_flush_input`, `sys_uart_set_baud`; the link lent (`sys_uart_link_direct`) |
| `sys_uart.c` | COBS and CRC16, and per port its provider, rings, RX and TX tasks; CREATE, the registry's destroy |
| `config/sys_uart_config.h` | The mode, the parity and the baud limits (published to the app) |
| `router/sys_uart_router.h` / `.c` | The remote interface (root key `ROUTER_KEY_UART` 0x13). The header publishes the op (`SYS_UART_OP_CREATE`) and the wire layouts (`sys_uart_cfg_t`, `sys_uart_port_args_t`) |
| `router/contract_uart.json` | The router's descriptor |
| `errors/` | `ERR_SYS_UART_MAP`, the messages |
| `Kconfig` | RX and TX tasks, the TX queue and its send wait, the driver's RX buffers; the link (`CONFIG_SYS_UART_LINK_*`) |
| `AGENTS.md` | Rules for changing this folder |

## Modes

A port's **mode** is set at CREATE and never changes:

| Mode | The line | Provider state | A session can attach it |
|---|---|---|---|
| `FRAMED` | **binary**: every frame goes COBS-encoded with its CRC16, between 0x00 bytes: `00 cobs([frame][crc16 LE]) 00` | `FRAMED`, `WIRED`, `UP` | yes: a client's console, a link to another board |
| `RAW` | the bytes as they are, both ways | `WIRED`, `UP` | no (`ERR_SESSION_NOT_FRAMED`): a stream's binding or the command binding |
| `RAW_LINE` | the same; an incoming message ends at a line feed | the same | no |
| `DIRECT` | the same, with no tasks and no queues of its own: its owner writes and reads on its own task | `WIRED`, `UP`, `NO_RX` | no: `ERR_PROV_NO_RX` for anyone who wants a hook (A DIRECT port) |

A raw port's **messages**: what comes in is cut into messages for the provider's user, each at most `CMD_DATA_MAX − 1` bytes (the longest payload of a stream, SYS_STREAM.MD): `RAW` ends a message after a pause (a read of `READ_WAIT_MS` = 10 ms with no byte), `RAW_LINE` at each `\n` (the sentences of a GPS; the terminator is part of the message; a line longer than the maximum is cut there). What goes out is written as it is, one queued message after the other, with no frame, CRC or delimiter, at the port's rate. A raw port is the stream's example in SYS_STREAM.MD: an IN stream bound to it, the app subscribed.

## Operation

- **RX**: a framed port takes the bytes up to each 0x00 as one candidate: COBS-decoded in its own buffer, its CRC16-CCITT (poly 0x1021, init 0xFFFF) checked, then delivered whole to the provider's user (`sys_data_provider_deliver`: the session). Anything else (the ROM's text, a panic, typing in a monitor) fails and is dropped without a word: the host's tool shows those bytes as text. A candidate longer than any frame is dropped at its 0x00. Each input (UART, USB-Serial-JTAG) has its own candidate buffer.
- **TX**: the provider's `send` only queues the frame with its CRC (a raw port's bytes without); `SYS_DATA_PROVIDER_SEND_URGENT` puts it in a queue of its own that the TX task writes first. Without `NOWAIT` a send waits up to `CONFIG_SYS_UART_SEND_WAIT_MS` for room, else (and at once with `NOWAIT`) `ERR_PROV_BUSY`. The TX task (`CONFIG_SYS_UART_TX_TASK_PRIO`, low) encodes each frame and writes it, a 0x00 before and after it, so a text line before it ends there.
- **Two outputs** (the console only): the frame goes to the UART and, when a host is connected, to the USB-Serial-JTAG (in pieces of half its TX buffer, which takes no longer write). The UART at 115200 baud is far slower than USB: the UART driver has a TX buffer of its own, and a frame that does not fit it now is not written to the UART (its host misses it, as a dropped frame), so a USB host is never held to the UART's pace.
- **The log** goes as frames only, on the LOGS stream to the clients that subscribe to it (SYS_STREAM.MD): `sys_log`'s `mirror_serial` is off by default (SYS_LOG.MD). The boot's text before the log hook, the ROM and a panic are text on the line, between the frames.
- **Initialization** (`sys_uart_init`): installs the console's UART driver (RX buffer and TX buffer; `stdout` moves to the driver) and the USB-Serial-JTAG driver, adds the console's provider and starts its TX and RX tasks; with `CONFIG_SYS_UART_LINK` it makes the link the way CREATE makes a port. Before the session attaches them.

Frame format above the line: SYS_SESSION.MD "Frames".

## Ports made at run time

A port other than the console's has no static state: `sys_uart_create` takes its memory (the port's state, two rings of `CONFIG_SYS_UART_TX_QUEUE` and the urgent size, the driver's RX buffer `CONFIG_SYS_UART_PORT_RX_BUF`, two tasks) and the provider's destroy gives it all back.

- **Create** checks its frame once (SYS_UART.MD "Remote"), installs the UART driver at the rate, 8 data bits, the parity and stop bits, with RTS / CTS when both are given, sets the pins, starts the tasks and adds the provider. The first failure frees what was made. A port made already (`ERR_UART_PORT_EXISTS`, also a UART whose driver somebody else installed), the console's or a UART the chip lacks (`ERR_UART_BAD_PORT`).
- **Remove** is the provider's REMOVE, by its id: refused while a session, a binding or the command binding has the provider open (`ERR_PROV_IN_USE`) and for the board's own ports (`ERR_PROV_SYSTEM`). Its destroy tells the tasks to come out (each says so, at most `STOP_WAIT_MS` waited; one that does not is deleted), deletes the UART driver and frees the rings and the state. It waits on the removing task, a router task, for a few milliseconds.
- **The pins** are `sys_io_pin_t` of the ESP32-S3's GPIO device (which must exist), as the I2C bus's: the UART reaches its pads through the GPIO matrix, so a pin of an expander is `ERR_UART_BAD_PIN`. RTS and CTS are both a pin or both `SYS_IO_PIN_NONE`. The firmware does not check a pin against what else uses it; the driver refuses a GPIO a UART cannot take (`ERR_UART_INIT`).
- **The rate** is a number, `SYS_UART_BAUD_MIN` (1200) to `SYS_UART_BAUD_MAX` (5000000), not a list: DMX is 250000 and MIDI 31250, which no list of the usual rates has. The app offers the usual ones. A rate the clock divider cannot make exactly runs at the nearest it can. The ISR in IRAM (`CONFIG_UART_ISR_IN_IRAM`) is the link's overlay's choice and applies to every port made.

## A DIRECT port

For a device driver and a protocol with exact timing (Modbus RTU's silence, a short request and answer). **Still a provider, by the same id:** LIST, REMOVE, a taken id and its owner work as for any port. There are only no TX and RX tasks and no rings, so nothing stands between the caller and the UART driver.

```c
ERR_TRY(sys_data_provider_open(id, NULL, NULL));          // the driver holds it: no one else, and no REMOVE
ERR_TRY(sys_uart_flush_input(id));                        // what came before is not the answer
ERR_TRY(sys_uart_write(id, request, len, 50));            // returns when the last byte has left the line
ERR_TRY(sys_uart_read(id, buf, sizeof(buf), &n, 100, 4)); // at cap, at a 4 ms pause, or nothing in 100 ms
```

- `sys_uart_read` returns as soon as `cap` bytes came, so a reply of known length costs nothing extra. A pause (`idle_ms`) is seen between one and two of it, at the resolution of a FreeRTOS tick.
- **It runs on the caller's task, so the wait is the caller's.** A device driver's op runs under the device registry's lock (SYS_DEVICE.MD "Concurrency"): a long read there stops every other device. A driver that waits long reads on a task of its own and keeps the result for its op to read.
- No hook: an IN stream, a session and the command binding are refused (`ERR_PROV_NO_RX`). `sys_uart_transact` and UART RAW work on it, with the bytes that came before dropped first. A stream's or SEND's bytes go into the driver's TX buffer (`UART_TX_BUF`), `ERR_PROV_BUSY` when it has no room.
- A half-duplex line's direction pin (RS-485) is not set up here: it is the driver's, around its write, or a later `uart_set_mode`.

## A command and its answer

`sys_uart_transact(provider, tx, tx_len, rx, rx_cap, &rx_len, wait_ms)` is what `sys_i2c_reg_read` is on I2C: bytes out, then the first message back (a device driver in C, and RAW and RECEIVE below). No second way to the port: it uses the provider's own open, send and close.

- It holds the rx hook for that time, so a provider someone reads from is `ERR_PROV_IN_USE`, and a framed one `ERR_UART_NOT_RAW`.
- On a raw port its bytes go through the port's queue and tasks, so an answer ends some 10 to 20 ms after its last byte (`RAW`) and a driver with exact timing uses a DIRECT port.
- **The answer is one message**: up to a pause of `READ_WAIT_MS` (10 ms) on `RAW`, a line on `RAW_LINE`. Nothing in `wait_ms` is no error: the answer is empty. A longer answer is cut at `rx_cap`.
- **It waits on the caller's task**, at most `SYS_UART_RAW_WAIT_MAX_MS` (1000): from the router, the work task, so normal commands wait behind it and urgent ones do not. One runs at a time.
- What comes when no one holds the hook is dropped: a device that talks by itself is followed with an IN stream (SYS_STREAM.MD) and written with the provider's SEND.

## The link to another board

`CONFIG_SYS_UART_LINK` makes a second provider: `CONFIG_SYS_UART_LINK_PORT` (UART1), framed and `WIRED`, a port of the kind above with `STATE_SYSTEM`. Every board attaches it as a slave's session at boot (RUNIT.MD "Boot"). A link on another UART is a framed CREATE and a SESSION ATTACH, which a board's profile can hold (SYS_BOARD.MD).

- **Wiring**: this board's link to the other board's **console UART** (UART0: U0RXD GPIO 44, U0TXD GPIO 43, the pins its ROM bootloader listens on): link TX (GPIO 18, `CONFIG_SYS_UART_LINK_TX_PIN`) to U0RXD (the devkit's RX pin), link RX (GPIO 17, `_RX_PIN`) to U0TXD (its TX pin), the grounds joined. The other board's console session hears this board's hello and makes this board its master (SYS_SESSION.MD "Boards, clients, the star"); the same wires let this board flash it, with two more for its BOOT and EN (SYS_FLASHER.MD). 17 and 18 are free on the ESP32-S3 devkit and on the runIT board (its GPIO header); the firmware uses none of them (the I2C buses are 15/16 and 40/41). The other board's log text goes out on that UART too: candidates that fail their CRC, dropped here.
- **Rate**: `CONFIG_SYS_UART_LINK_BAUD` (921600), the console's rate on the other board (`CONFIG_ESP_CONSOLE_UART_BAUDRATE`): the same firmware, so the same number. RTS / CTS pins are optional (`_RTS_PIN`, `_CTS_PIN`, -1 = none).
- **A flash write** (NVS) stops the cache for milliseconds, longer than the UART's 128-byte FIFO takes to fill at that rate: the RX interrupt runs from IRAM (`CONFIG_UART_ISR_IN_IRAM`, in `sdkconfig`) and the driver's RX buffer is `CONFIG_SYS_UART_PORT_RX_BUF` (4 KB).
- **TX**: unlike the console's, a port's UART driver has no TX buffer: its TX task writes a frame to the FIFO and waits there until it is out (nothing but its own queue waits behind it), so its queue is the only one and a frame is copied once less.
- Noise on the line (a cable pulled, a board resetting) is a candidate that fails its CRC and is dropped; the session's timeouts do the rest.

### The link lent

`sys_uart_link_direct(true)` lends the link as a DIRECT port to one owner, sys_flasher (SYS_FLASHER.MD): the provider goes down (its session ends; sends are refused, `ERR_PROV_DOWN`), its tasks stop, what was queued is dropped, and the owner writes and reads on its own task with `sys_uart_write`, `sys_uart_read`, `sys_uart_flush_input` and `sys_uart_set_baud` under the link's provider id. `sys_uart_link_direct(false)` takes it back: the link's rate, its tasks again, the provider up, so its session says hello. The port and its provider stay all the while: nothing is removed or made again.

## Remote (`sys_uart_router`, root key `ROUTER_KEY_UART` = 0x13)

Requests are `[0x13][dir][op][args]` (`dir` 0 read, 1 write; ROUTER.MD). The op and its layouts are in `sys_uart_router.h`; `contract_uart.json` names them.

| Op | Side | Request | Response |
|---|---|---|---|
| `0x01` CREATE | W | `sys_uart_cfg_t` (21 B): `u8 provider, u8 port, u32 baud, u8 mode (sys_uart_mode_e), u8 parity (sys_uart_parity_e), u8 stop_bits (1..2), sys_io_pin_t tx, rx, rts, cts` | – |
| | R | `u8 port` | `sys_uart_cfg_t`: what the port was made with (`ERR_UART_BAD_PORT`: none made so; the console's and the link's are the board's own) |

| `0x02` RAW | W | `sys_uart_raw_args_t` `{u8 provider, u16 wait_ms, u16 rx_max}`, then the bytes (≥ 1) | the first message back; empty when nothing came |
| `0x03` RECEIVE | R | `sys_uart_raw_args_t` | the same, with nothing sent |

Example — `AT\r\n` to a module on provider 0x10, waiting 300 ms for at most 256 B: `13 01 02 10 2C 01 00 01 41 54 0D 0A`.

Example — a GPS on UART 2 (pins 4 and 5 of device 0) read by line, to a stream the app subscribes: CREATE provider 0x10 on port 2, 9600 baud, `RAW_LINE`, no parity, 1 stop bit, TX (device 0, pin 4), RX (device 0, pin 5), no RTS / CTS (`SYS_IO_PIN_NONE`: device `0xFFFF`, pin `0xFF`): `13 01 01 10 02 80 25 00 00 02 00 01 00 00 04 00 00 05 FF FF FF FF FF FF`; then `04 01 01 10 01 00` (STREAM CREATE 0x10, IN) and `04 01 03 10 10` (BIND stream 0x10 to provider 0x10).

## Errors (`ERR_MOD_SYS_UART` = 24)

`ERR_UART_BAD_ARG`, `_NOT_RAW{provider}`, `_NOT_DIRECT{provider}`, `_TIMEOUT{provider}`, `_BAD_PORT{port}`, `_PORT_EXISTS{port}`, `_BAD_BAUD{baud}`, `_BAD_FORMAT{mode, parity, stop_bits}`, `_BAD_PIN{device, pin}`, `_INIT{port}` (over `ERR_ESP_ERR`: the driver refused), `_NO_MEM{port}`. A send's failure is the provider's `ERR_PROV_*` (SYS_DATA_PROVIDER.MD).

## Limits

- Largest frame `CMD_FRAME_MAX`; on the line at most `CMD_FRAME_MAX` + 2 (CRC) + 3 (COBS) + 2 (the 0x00s).
- `CONFIG_SYS_UART_TX_QUEUE` (8 KB) is larger than the stream queue, so a burst of PUSH frames fills that one first and its producer is refused (and resends), not lost here unseen. Every port made has one, so a made port costs about this plus the RX buffer and the two tasks' stacks of heap.
- At most the chip's UARTs less the console's; `CONFIG_SYS_DATA_PROVIDER_MAX` (8) slots for providers in all.

## Tests

`tree_tests.py` (`.claude/skills/runit-esp/scripts/`) runs over a link between two boards (build.md "Two boards on a cable"). The host scripts (`router_link.py`) open a client session over this line (the same COBS and CRC16); `router_tests.py`, `board_tests.py` and `vm_suite.py` run over it. `serial_capture.py` shows the raw bytes. CREATE, the raw modes and the raw command are not covered by a suite yet; the app's `tests/app/serial.test.ts` holds their frames.
