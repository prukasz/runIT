# sys_uart — the board's UART ports as providers

Component `runit-esp-rework/components/sys/bus/sys_uart/`. Makes the board's own UART ports **providers** (SYS_DATA_PROVIDER.MD):

| Port | Provider | Made by | Mode |
|---|---|---|---|
| **The console's** (GPIO 43/44 via the board's USB-UART bridge at `CONFIG_ESP_CONSOLE_UART_BAUDRATE`, and the native USB-Serial-JTAG when it is a console output) | what the board gives `sys_uart_init` | `sys_uart_init`, always | framed |
| **The link to another board** (`CONFIG_SYS_UART_LINK`, on by default) | what the board gives `sys_uart_init` | `sys_uart_init`, from Kconfig | framed; `DIRECT` while lent |

Both are the board's own (`STATE_SYSTEM`: never removed) and wired (`STATE_WIRED`). The ESP32-S3 has three UARTs (0, 1, 2): the console takes `CONFIG_ESP_CONSOLE_UART_NUM` (0), the link `CONFIG_SYS_UART_LINK_PORT` (1). **A UART is one provider and a provider has one reader** (a session, or the command binding): a line that must carry commands, every stream and the watches at once is one framed port with one session (SYS_SESSION.MD), whose frames name the destination and the stream.

## Files

| File | Role |
|---|---|
| `sys_uart.h` | API: `sys_uart_init`; a DIRECT port's `sys_uart_write`, `sys_uart_read`, `sys_uart_flush_input`, `sys_uart_set_baud`; the link lent (`sys_uart_link_direct`) |
| `sys_uart.c` | COBS and CRC16; per port its provider, rings, RX and TX tasks; the link's UART, its pins reserved, its tasks parked while it is lent |
| `config/sys_uart_config.h` | The modes (`FRAMED`, `RAW`, `RAW_LINE`, `DIRECT`), the parity, the wait and baud limits (published to the app; the UART contract's too) |
| `errors/` | `MODULE_ID_UART` (0x13, `ERR_MOD_SYS_UART`, also the UART contract's ID: SYS_UART_CONTRACT.MD), `ERR_SYS_UART_MAP`, the messages |
| `Kconfig` | RX and TX tasks, the TX queue and its send wait, the driver's RX buffers; the link (`CONFIG_SYS_UART_LINK_*`) |
| `AGENTS.md` | Rules for changing this folder |

## The line

**Binary**: every frame goes COBS-encoded with its CRC16, between 0x00 bytes: `00 cobs([frame][crc16 LE]) 00`, CRC16-CCITT (poly 0x1021, init 0xFFFF, no reflection, no final xor). COBS leaves no other 0x00, so a frame ends at the next one; COBS adds at most one byte in 254.

- **RX**: the bytes up to each 0x00 are one candidate: COBS-decoded in its own buffer, its CRC checked, then delivered whole to the provider's user (`sys_data_provider_deliver`). Anything else (the ROM's text, a panic, typing in a monitor, the log's text) fails and is dropped without a word: the host's tool shows those bytes as text. A candidate longer than any frame is dropped at its 0x00. Each input (UART, USB-Serial-JTAG) has its own candidate buffer.
- **TX**: the provider's `send` gets the frame as its two parts (SYS_DATA_PROVIDER.MD "Use") and queues `[head][body][crc16]` as one item, the CRC run over both parts, so the TX task encodes the item as it is. `SYS_DATA_PROVIDER_SEND_URGENT` puts it in a queue of its own that the TX task writes first. Without `NOWAIT` a send waits up to `CONFIG_SYS_UART_SEND_WAIT_MS` for room, else (and at once with `NOWAIT`) `ERR_PROV_BUSY`. `SPARE`: refused when the item would fill its ring past half (`sys_data_provider_spare_refused`, in bytes of the ring; the free size is a snapshot, so two sends at once may pass it by one item). The TX task (`CONFIG_SYS_UART_TX_TASK_PRIO`, low) encodes each frame and writes it, a 0x00 before and after it, so a text line before it ends there.
- **Two outputs** (the console only): the frame goes to the UART and, when a host is connected, to the USB-Serial-JTAG (in pieces of half its TX buffer). The UART is far slower than USB: its driver has a TX buffer of its own, and a frame that does not fit it now is not written to the UART (its host misses it, as a dropped frame), so a USB host is never held to the UART's pace.
- **Initialization** (`sys_uart_init`): installs the console's UART driver (RX buffer and TX buffer; `stdout` moves to the driver) and the USB-Serial-JTAG driver, adds the console's provider and starts its tasks; with `CONFIG_SYS_UART_LINK` it makes the link. At boot, before a user opens them and before the GPIO device is made.

The frames above the line: SYS_SESSION.MD "Frames" (a session), or the command binding's `[command]` / `[status][data]` (SYS_DATA_PROVIDER.MD "Command binding").

## The link to another board

`CONFIG_SYS_UART_LINK` makes a second provider on `CONFIG_SYS_UART_LINK_PORT` (UART1), framed and wired. A slave's session attaches it.

- **Wiring**: this board's link to the other board's **console UART** (UART0: U0RXD GPIO 44, U0TXD GPIO 43, the pins its ROM bootloader listens on): link TX (GPIO 18, `CONFIG_SYS_UART_LINK_TX_PIN`) to U0RXD (the devkit's RX pin), link RX (GPIO 17, `_RX_PIN`) to U0TXD (its TX pin), the grounds joined. The same wires let this board flash it, with two more for its BOOT and EN (SYS_FLASHER.MD). 17 and 18 are free on the ESP32-S3 devkit and on the runIT board (its GPIO header). The other board's log text goes out on that UART too: candidates that fail their CRC, dropped here.
- **Rate**: the console's, `CONFIG_ESP_CONSOLE_UART_BAUDRATE`: the far end is the other board's console, and every board runs the same firmware, so the two rates can't differ. RTS / CTS pins are optional (`_RTS_PIN`, `_CTS_PIN`, -1 = none; both, or neither).
- **Its pins are reserved in ESP-IDF** (`esp_gpio_reserve`) when it is made, so the GPIO device refuses them (`ERR_DEV_PIN_LOCKED`, owner `SYS_DEVICE_ID_NONE`: no device's; SYS_IO.MD). They must stay clear of the console's, the USB's (19/20), the strapping pins (0, 3, 45, 46), the flasher's wires and the board's I2C and expander pins.
- **A flash write** (NVS) stops the cache for milliseconds, longer than the UART's 128-byte FIFO takes to fill at a high rate: the RX interrupt runs from IRAM when `CONFIG_UART_ISR_IN_IRAM` is set, and the driver's RX buffer is `CONFIG_SYS_UART_PORT_RX_BUF` (4 KB).
- **TX**: unlike the console's, the link's UART driver has no TX buffer: its TX task writes a frame to the FIFO and waits there until it is out (nothing but its own queue waits behind it), so its queue is the only one.
- Noise on the line (a cable pulled, a board resetting) is a candidate that fails its CRC and is dropped; the session's timeouts do the rest.

### The link lent

`sys_uart_link_direct(true)` lends the link as a DIRECT port to one owner, sys_flasher (SYS_FLASHER.MD): the provider goes down (its session ends; sends are refused, `ERR_PROV_DOWN`), its two tasks **park** (never deleted: each tells the lender as it parks, the RX task within its 10 ms read, the TX task after the frame it writes), what was queued is dropped, and the owner writes and reads on its own task with `sys_uart_write`, `sys_uart_read`, `sys_uart_flush_input` and `sys_uart_set_baud` under the link's provider id. A task that does not park in `PARK_WAIT_MS` (2 s: a TX task held by a CTS that never comes) fails the lending with `ERR_UART_TIMEOUT`: the tasks are woken and the provider is up again. `sys_uart_link_direct(false)` takes it back: the console's rate, the input flushed, the tasks woken, the provider up, so its session says hello. The port and its provider stay all the while.

```c
ERR_TRY(sys_uart_link_direct(true));
ERR_TRY(sys_data_provider_open(link, NULL, NULL));       // the owner holds it: no one else
ERR_TRY(sys_uart_flush_input(link));                     // what came before is not the answer
ERR_TRY(sys_uart_write(link, request, len, 50));         // returns when the last byte has left the line
ERR_TRY(sys_uart_read(link, buf, sizeof(buf), &n, 100, 4)); // at cap, at a 4 ms pause, or nothing in 100 ms
```

- `sys_uart_read` returns as soon as `cap` bytes came, so a reply of known length costs nothing extra. A pause (`idle_ms`) is seen between one and two of it, at the resolution of a FreeRTOS tick.
- **It runs on the caller's task, so the wait is the caller's**: never under a lock others wait for.

## Errors (`ERR_MOD_SYS_UART` = `MODULE_ID_UART` 0x13)

`ERR_UART_BAD_ARG`, `_BAD_PORT{port}` (the link lent on a board without one), `_BAD_BAUD{baud}`, `_INIT{port}` (the driver refused the link's UART), `_NOT_DIRECT{provider}`, `_TIMEOUT{provider}` (a write did not leave the line; the link's tasks did not park); the UART contract's `_IN_USE{port, owner}` (the port has a reader or is lent) and `_NOT_OWNER{port, owner}` (SYS_UART_CONTRACT.MD). A send's failure is the provider's `ERR_PROV_*` (SYS_DATA_PROVIDER.MD).

## Limits

- Largest frame `CMD_FRAME_MAX`; on the line at most `CMD_FRAME_MAX` + 2 (CRC) + 3 (COBS) + 2 (the 0x00s).
- `CONFIG_SYS_UART_TX_QUEUE` (8 KB) per port, plus an urgent queue of two whole frames; all static.

## Tests

`python runit-esp-rework/tools/session_client.py <COM port>` opens a session over the console (the rework's `main` attaches it `UP`) and runs PING and PEERS: the line in both directions, the CRC over the two parts (SYS_SESSION.MD "Tests"). The link lent is covered by the flasher's test once sys_flasher is in the rework.

## Open

- **Ports as devices**: the UART contract is written (`sys_uart_contract`, SYS_UART_CONTRACT.MD); its port, `device_esp32s3_uart` in `devices/soc/` beside `device_esp32s3_i2c`, is not. It makes the link (UART1, a board's device: `SYS_DEVICE_CREATE_SYSTEM`) and a port on UART2: CREATE through the registry, its pins claimed for it (`sys_io_claim_peripheral`), its provider made in its create(). This component then keeps the console and the line's engine (COBS, CRC, the queues, the static tasks per controller), and the link's Kconfig, its `esp_gpio_reserve` and `sys_uart_link_direct` go (PROPOSAL_NETWORK.md section 6, stage 3a).
- **The RX task's read waits for a full chunk**: `uart_read_bytes(chunk, sizeof(chunk), 10 ms)` returns a short frame only after its 10 ms. The read goes to the driver's event queue (`uart_driver_install` with a queue, the RX timeout at 1–2 characters) and the USB-Serial-JTAG input to a task of its own.
