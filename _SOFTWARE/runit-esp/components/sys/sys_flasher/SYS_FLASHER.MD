# sys_flasher — programming the board on the link

Component `components/sys/sys_flasher/`. The board writes its own firmware into the ESP32-S3 on its link, through that chip's ROM bootloader: the first firmware of a new board, or a board whose firmware no longer starts. Every later update of that board is the usual OTA (SYS_OTA.MD), sent through this board to it. The same firmware runs on every board (one build), so copying what runs here is all a new board needs.

```
 this board                                              the board on the link (ESP32-S3)
 ──────────                                              ───────────────────────────────
 GPIO 18 (link TX) ───────────────────────────────────►  U0RXD (GPIO 44)   ┐ its console UART: the ROM bootloader's,
 GPIO 17 (link RX) ◄───────────────────────────────────  U0TXD (GPIO 43)   ┘ then its runIT session to this board
 GPIO 46 (BOOT) ──────────────────────────►  GPIO 0
 GPIO 3  (EN) ────────────────────────────►  EN (RST)
 GND ─────────────────────────────────────────────────── GND
```

## Files

| File | Role |
|---|---|
| `sys_flasher.h` / `.c` | Init (the wires), START, ABORT, STATUS; the job on its own task |
| `rom/sys_flasher_rom.h` / `.c` | The ROM's protocol as pure functions: SLIP, a command's packet, its checksum, an answer's parse, the MD5 in hex. Host-tested (`host/`) |
| `router/sys_flasher_router.h` / `.c` | The remote interface (`ROUTER_KEY_FLASHER` 0x14): the ops and their layout |
| `router/contract_flasher.json` | The router's descriptor |
| `errors/` | `ERR_SYS_FLASHER_MAP`, the messages |
| `Kconfig` | The EN and BOOT pins (3, 46), the rate while flashing (921600), the task |
| `host/` | Host tests of `rom/` (`python test_host/run.py sys_flasher`) |

## A flashing

START answers at once (CMD.MD "Long work"); STATUS follows it. Refused while this board's id is `SYS_SESSION_ID_DEFAULT` (0): a new board starts with it (`ERR_FLASHER_BOARD_ID`), so this board needs another first (SESSION ID, then a restart). Before the job starts, START reads what it copies: this board's running app and its image length (`esp_image_verify`), the partitions, the flash size (`ERR_FLASHER_IMAGE` when one can't be read). Then, on the flasher's task:

1. **The link lent** (`sys_uart_link_direct(true)`, SYS_UART.MD "The link lent"): its session ends, its tasks stop, the port is read and written on this task.
2. **Connect** (step `CONNECT`): 115200; BOOT low, EN pulsed, BOOT released, so the chip starts its ROM bootloader; what it says as it starts (300 ms) is logged (`flasher: the board on the link said …`); SYNC until it answers (the ROM learns the rate from it); CHANGE_BAUD to `CONFIG_SYS_FLASHER_BAUD`; SPI_ATTACH; SPI_SET_PARAMS with this board's flash size (the same partition table on both).
3. **Bootloader** (`BOOTLOADER`): this board's flash from 0x0 to the end of the partition table (`CONFIG_PARTITION_TABLE_OFFSET` + 0x1000) to the same place.
4. **Erase** (`ERASE`): `nvs` and `otadata`, so the new board starts with id 0 and no settings, from `ota_0`.
5. **Firmware** (`APP`): the running app (whichever slot it runs from) into `ota_0`.
6. **Restart** (`RESTART`): BOOT high, EN pulsed: the board starts its firmware, also after a failure (its old one, or the new one). The link is taken back (`sys_uart_link_direct(false)`): its session says hello, and the new board's hello opens it (SYS_SESSION.MD "Boards, clients, the star").

Each written region is checked: SPI_FLASH_MD5 of the other chip against the MD5 of what was sent (`ERR_FLASHER_VERIFY`). ABORT stops between two packets (`ERR_FLASHER_ABORTED`). An error ends the job `FAILED`; its chain is reported (the errors stream) and its root tag kept for STATUS.

## The ROM's protocol

Each command is a SLIP packet (0xC0 at each end, 0xC0 and 0xDB escaped): `[0x00][op][size u16][checksum u32][data]`; each answer `[0x01][op][size u16][value u32][data][status][error][0][0]` (the ESP32-S3 ROM's four status bytes). Bytes outside a packet (the ROM's boot text) are skipped. Ops used: SYNC 0x08 (`07 07 12 20` and 32 × 0x55), CHANGE_BAUD 0x0F (`{new rate, 0}`), SPI_ATTACH 0x0D (`{0, 0}`), SPI_SET_PARAMS 0x0B (`{0, size, 64 KB, 4 KB, 256, 0xFFFF}`), FLASH_BEGIN 0x02 (`{size, packets, 0x400, offset, 0}`: the ROM erases the region first, so with no packets it only erases), FLASH_DATA 0x03 (`{0x400, seq, 0, 0}` and a 0x400-byte block, the last padded with 0xFF; checksum: the block's bytes XORed onto 0xEF), SPI_FLASH_MD5 0x13 (`{offset, size, 0, 0}` → 32 hex digits). Waits: an answer 3 s, FLASH_BEGIN 30 s per MB, the MD5 8 s per MB.

## The wires

`CONFIG_SYS_FLASHER_EN_PIN` (3) and `_BOOT_PIN` (46), push-pull, read back: `sys_flasher_init` drives both high and they stay driven. They were open drain first, and then a released wire read low with the other board powered (measured: the line is held down on the PCB's header), so it stayed in reset; a driven high does not. The other board's RST and BOOT buttons are not used while the wires are joined (they would fight the output). Each flashing logs the levels it reads with both driven high (`the lines driven high: EN 1, BOOT 1`): 0 is a hold or a short. Neither is reserved in the firmware's pins (`SYS_PIN_*`): a board that flashes leaves them unused.

The other board is powered by its native USB or 5 V, not by its USB-UART bridge's port: a powered bridge drives U0RXD too.

## Remote (`sys_flasher_router`, root key `ROUTER_KEY_FLASHER` 0x14)

| Op | Side | Args | Answer |
|---|---|---|---|
| `0x01` START | W | – | – |
| `0x02` STATUS | R | – | `sys_flasher_status_reply_t`: `u8 state` (`sys_flasher_state_e`: IDLE, RUNNING, DONE, FAILED), `u8 step` (`sys_flasher_step_e`: CONNECT, BOOTLOADER, ERASE, APP, RESTART), `u32 done`, `u32 total` (bytes written and checked, of all), `u32 error` (FAILED: its root tag) |
| `0x03` ABORT | W | – | – |

The app: `flashAttached` in `runit-app/src/backend/session/flasher.ts` (START, then STATUS until it ends).

## Errors (`ERR_MOD_SYS_FLASHER` = 25)

`ERR_FLASHER_BUSY`, `ERR_FLASHER_BOARD_ID`, `ERR_FLASHER_NO_ANSWER{op, bytes}` (`bytes`: what the line brought since the link was lent; 0 is a silent line: the other board's power, the ground, its U0TXD wire or EN), `ERR_FLASHER_ROM{op, code}`, `ERR_FLASHER_VERIFY{addr}`, `ERR_FLASHER_IMAGE`, `ERR_FLASHER_ABORTED`; the port's own `ERR_UART_*`.

## Needs elsewhere

- **sys_uart**: `sys_uart_link_direct`, a DIRECT port's `sys_uart_write`, `_read`, `_flush_input`, `sys_uart_set_baud`.
- **sys_session**: this board's id; the link's session ends and opens again by itself (the provider down, then up).
- **runit**: `sys_flasher_init(RUNIT_BOARD_PROVIDER_UART_LINK)` after the uart, the router under `ROUTER_KEY_FLASHER`.

## Tests

`python test_host/run.py sys_flasher` (the protocol). On boards: `HW_FLASHER=1 npm run test:hw` in `runit-app` runs `n-flasher.hw.test.ts` (wired as above, this board's id not 0): the flashing ends DONE, every byte written, the new board opens a session on the link with id 0 and answers a PING through this board.

## Open

- The firmware copied is this board's: an image sent by the app to be written to the other board is not done.
- The ROM's own FLASH_DATA is used (no stub loader): no compression, about 1.5 MB in some 20 s at 921600.
