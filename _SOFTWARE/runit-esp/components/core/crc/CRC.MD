# crc — the board's CRCs

Component `runit-esp/components/core/crc/`. The calls (`crc.h`), with the chip's implementation behind them, as `nvs_store` keeps its API apart from ESP-IDF's NVS.

| File | Role |
|---|---|
| `crc.h` | `crc32_ieee(crc, data, len)`: CRC-32 of zlib / IEEE (reflected `0xEDB88320`), going on from `crc` (0 starts one) |
| `esp/crc_esp.c` | This chip's: the ROM's `esp_rom_crc32_le` |

Every CRC-32 of the firmware is this one: `nvs_store` (a file's), `sys_actions` (the records' image), `sys_watch` (an `ON_CHANGE` answer). A host build gives `esp_rom_crc32_le` itself (`sys_actions/host/host_nvs.c`).
