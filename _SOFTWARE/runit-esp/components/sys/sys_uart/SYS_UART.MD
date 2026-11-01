# sys_uart — the console UART and the board-to-board link as providers

Component `components/sys/sys_uart/`. Makes the console UART (GPIO 43/44 via the board's USB-UART bridge, 115200 baud) and, when it is a console output, the native USB-Serial-JTAG, one **provider** (SYS_DATA_PROVIDER.MD): `SYS_UART_PROVIDER` = `{SYS_DATA_PROVIDER_LINK_UART, the console's port}`, framed, wired, always up (`max_frame` = `CMD_FRAME_MAX`). The board config attaches a client session to it (SYS_SESSION.MD), so the console is a client's way to the board like the BLE pair.

The line is **binary**: every frame goes COBS-encoded with its CRC16, between 0x00 bytes.

```
00 cobs([frame][crc16 LE]) 00
```

## Files

| File | Role |
|---|---|
| `sys_uart.h` | API, `SYS_UART_PROVIDER`, `SYS_UART_LINK_PROVIDER` |
| `sys_uart.c` | COBS and CRC16, and per port (the console, the link) its provider, rings, RX and TX tasks |
| `Kconfig` | RX and TX tasks, the TX queue and its send wait, the driver's RX buffer; the link (`CONFIG_SYS_UART_LINK_*`) |
| `AGENTS.md` | Rules for changing this folder |

## Operation

- **RX**: the bytes up to each 0x00 are one candidate: COBS-decoded, its CRC16-CCITT (poly 0x1021, init 0xFFFF) checked, then delivered whole to the provider's user (`sys_data_provider_deliver`: the session). Anything else (the ROM's text, a panic, typing in a monitor) fails and is dropped without a word: the host's tool shows those bytes as text. A candidate longer than any frame is dropped at its 0x00. Each input (UART, USB-Serial-JTAG) has its own candidate buffer.
- **TX**: the provider's `send` only queues the frame; `SYS_DATA_PROVIDER_SEND_URGENT` puts it in a queue of its own that the TX task writes first. Without `NOWAIT` a send waits up to `CONFIG_SYS_UART_SEND_WAIT_MS` for room, else (and at once with `NOWAIT`) `ERR_PROV_BUSY`. The TX task (`CONFIG_SYS_UART_TX_TASK_PRIO`, low) encodes each frame and writes it, a 0x00 before and after it, so a text line before it ends there.
- **Two outputs**: the frame goes to the UART and, when a host is connected, to the USB-Serial-JTAG (in pieces of half its TX buffer, which takes no longer write). The UART at 115200 baud is far slower than USB: the UART driver has a TX buffer of its own, and a frame that does not fit it now is not written to the UART (its host misses it, as a dropped frame), so a USB host is never held to the UART's pace.
- **The log** goes as frames only, on the LOGS stream to the clients that subscribe to it (SYS_DATA_CONNECTOR.MD): `sys_log`'s `mirror_serial` is off by default (SYS_LOG.MD). The boot's text before the log hook, the ROM and a panic are text on the line, between the frames.
- **Initialization** (`sys_uart_init`): installs the UART driver (RX buffer and TX buffer; `stdout` moves to the driver) and the USB-Serial-JTAG driver, adds the provider, and starts the TX and RX tasks. Before the session attaches it.

Frame format above the line: SYS_SESSION.MD "Frames".

## The link to another board

`CONFIG_SYS_UART_LINK` adds a second provider, `SYS_UART_LINK_PROVIDER` = `{SYS_DATA_PROVIDER_LINK_UART, CONFIG_SYS_UART_LINK_PORT}`: a UART of its own (UART1) with the same line, framed and `WIRED`, with rings and RX and TX tasks of its own. A session attaches it as the board's parent or as a child (SYS_SESSION.MD); `RUNIT_LINK_ROOT` and `RUNIT_LINK_CHILD` do it at boot (RUNIT.MD "Boot").

- **Pins**: TX GPIO 17, RX GPIO 18 (`CONFIG_SYS_UART_LINK_TX_PIN` / `_RX_PIN`), the same on both boards, so the cable crosses them (TX to RX, RX to TX) and joins the grounds. They are free on the ESP32-S3 devkit and on the runIT board (its GPIO header); the firmware uses none of them (the I2C buses are 15/16 and 40/41). They are not the pins of a flasher: the devkit's bridge is on UART0 (GPIO 43/44), the runIT board flashes over its native USB (GPIO 19/20), and neither is a strapping pin, so a cable on the link does not disturb flashing or a reset.
- **Rate**: `CONFIG_SYS_UART_LINK_BAUD` (921600), the same on both boards. RTS / CTS pins are optional (`_RTS_PIN`, `_CTS_PIN`, -1 = none).
- **A flash write** (NVS) stops the cache for milliseconds, longer than the UART's 128-byte FIFO takes to fill at that rate: the RX interrupt runs from IRAM (`CONFIG_UART_ISR_IN_IRAM`, set by the link overlays) and the driver's RX buffer is `CONFIG_SYS_UART_LINK_RX_BUF` (4 KB).
- **TX**: unlike the console's, the link's TX task waits for room in the UART driver's TX buffer: nothing but its own queue waits behind it.
- Noise on the line (a cable pulled, a board resetting) is a candidate that fails its CRC and is dropped; the session's timeouts do the rest.

## Errors

No tags of its own (`ERR_MOD_SYS_UART`, 3, is unused): a send's failure is the provider's `ERR_PROV_*` (SYS_DATA_PROVIDER.MD).

## Limits

- Largest frame `CMD_FRAME_MAX`; on the line at most `CMD_FRAME_MAX` + 2 (CRC) + 3 (COBS) + 2 (the 0x00s).
- `CONFIG_SYS_UART_TX_QUEUE` (8 KB) is larger than the connector queue, so a burst of stream frames fills that one first and its producer is refused (and resends), not lost here unseen.

## Tests

`tree_tests.py` (`.claude/skills/runit-esp/scripts/`) runs over a link between two boards (build.md "Two boards on a cable").

The host scripts (`router_link.py` in `.claude/skills/runit-esp/scripts/`) open a client session over this line (the same COBS and CRC16); `router_tests.py`, `board_tests.py` and `vm_suite.py` run over it. `serial_capture.py` shows the raw bytes.
