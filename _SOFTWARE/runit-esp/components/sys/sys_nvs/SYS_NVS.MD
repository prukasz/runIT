# sys_nvs — NVS partitions and JSON files

Component `components/sys/sys_nvs/`. Makes the NVS partitions ready, and keeps a file too big for one blob as chunks. The modules still read and write their own values with `nvs.h`.

| File | Role |
|---|---|
| `sys_nvs.h` / `sys_nvs.c` | `sys_nvs_init`, `sys_nvs_json_*` |
| `errors/` | The error tags (`ERR_MOD_SYS_NVS`), `errors_sys_nvs.json` the messages |

## Partitions

`sys_nvs_init(partition)` is the only place that initialises or erases NVS, called by the boot step `nvs` (RUNIT.MD "Boot") for each partition. A partition NVS can't use (no free pages, a new version) is erased, with all it held; the modules only open their namespaces.

## JSON files

A file is one namespace of its own, opened at boot with `sys_nvs_json_open`, so another file takes only another name. The content is never parsed.

| Key | Holds |
|---|---|
| `c0`, `c1`, … | One chunk each, as `sys_nvs_json_append` got it. No buffer in RAM: a chunk is written as it comes |
| `info` | `sys_nvs_json_info_t`: length, CRC-32 (`esp_rom_crc32_le`, zlib / IEEE) and the number of chunks |

**Append.** The offset must be the file's length, so a chunk sent twice is refused (`ERR_NVS_OFFSET`). The chunk is stored first, then `info`: a power cut leaves the file without the chunk or with it whole. A write cut short leaves a shorter file: the reader compares length and CRC with what it sent. **Replace** is erase and append; nothing keeps the old content meanwhile.

**Read.** `sys_nvs_json_get` by chunk, `0..chunks-1`. A chunk must fit the reader's buffer, so a writer keeps its chunks no larger than a read can answer.

A file shares its partition (or has one of its own, the project's source: SYS_PROJECT.MD "The source"): when `sys_nvs_init` erases the partition the file goes too.
