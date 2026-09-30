# System Project Store (sys_project)

IMPORTANT: If you modify `sys_project.h` or `sys_project.c`, update this documentation.

## Overview

The **stored code**: the complete list of command frames that configures the board from its defaults (BLE services and characteristics, data connectors, devices, event subscriptions, the VM program). The app builds it from its project and writes it as data; the board keeps it in its own NVS partition and **replays it at every boot**. Reading it back is recovery (the app's project JSON is the source). Design: `app/docs/03_records_as_storage.md`.

Separate from [[SYS_ACTIONS.MD]]: actions are runtime recordings of live traffic, replayed on demand (2 KB each). The stored code is written, not recorded, so nothing runs while storing and a board already configured doesn't change what gets stored. Both share the record format and the replay path.

## Frame list

```
[u16 len LE][class][packet][payload]  ...   (no seq byte; len = 2..CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX - 1)
```

CRC-32 over the whole list: `esp_rom_crc32_le(0, …)` = zlib / IEEE CRC-32.

## Storage

NVS partition `CONFIG_SYS_PROJECT_PARTITION_LABEL` (`project`, 512 KB in `partitions.csv`), namespace `code`:

| Key | Contents |
|---|---|
| `a000`, `a001`, … / `b000`, … | Chunks of the list (`CONFIG_SYS_PROJECT_CHUNK_SIZE`, last one shorter), generation `a` / `b` |
| `active` | Header of the committed generation: magic, length, CRC-32, schema ID, frame count, generation |

A store fills the generation that isn't active; `commit` checks it and rewrites `active` in one NVS write, so a failed or interrupted store keeps the old code. The next `begin` erases the old generation. An unreadable partition is erased at init (it only holds a copy of the app's project).

Options live in [[SYS_SETTINGS.MD]] (default partition), key `prj_opts` `{autostart}`.

## API

| Function | Does |
|---|---|
| `sys_project_init()` | Boot step after `sys_settings_init()`: open the partition, load the header |
| `sys_project_begin(length)` | Start a store of `length` bytes (≤ `CONFIG_SYS_PROJECT_MAX_BYTES`); drops a store in progress |
| `sys_project_write(offset, data, len)` | Append; `offset` must equal the bytes received so far |
| `sys_project_commit(crc, schema_id, filter)` | All bytes received, `schema_id == SE_schema_id()`, CRC read back from flash, every frame 2..max bytes and accepted by `filter` → becomes the stored code |
| `sys_project_abort()` / `sys_project_erase()` | Drop a store in progress / remove the stored code (next boot: defaults only) |
| `sys_project_set_options(autostart)` | Store `prj_opts` |
| `sys_project_get_info(&info)` | Stored or not, length, frames, CRC, schema ID, capacity, autostart, last replay report |
| `sys_project_read(offset, out, len)` | Bytes of the stored list (recovery) |
| `sys_project_replay(is_vm_frame)` | Boot: schema check (mismatch → `SKIPPED`), CRC + framing check (→ `CORRUPT`), then every frame through `sys_interface_decode()` with RX suspended. A refused frame is reported (`SE_REPORT`) and counted; the replay continues. `is_vm_frame` marks VM frames whose refusal cancels autostart |
| `sys_project_report(&report, &autostart)` | Last replay's report, with the CRC of the code that boot replayed (`crc32`, 0 = none): the running config is the stored code only while it equals the stored CRC |
| `sys_project_load()` | Restart after `CONFIG_SYS_PROJECT_LOAD_DELAY_MS` (the response goes out first); the restart replays the code |

All calls come from the interface RX task, except the boot replay (RX suspended): no lock.

## Boot

`runit.c`: `sys_project_init` runs with the setup steps (after `sys_settings_init`); the runtime step `runit_project_boot` (after `vm_exec_start`, so every class's decoder and the VM task are ready) replays the code and, when `prj_opts.autostart` is set and no VM frame was refused, starts the VM (`VM_EXEC_NORMAL_MODE`). A replay failure never aborts the boot.

## Wire (class `0x0A`)

`dec_settings_project.h`, published to `data-structures/settings/settings.generated.json` (tag `project`). Commit's filter refuses frames of class `0x0A` itself, action record start / stop / remove, and VM exec (`0x48`, autostart is `prj_opts`).

| Packet | Byte | Payload | Response |
|---|---|---|---|
| begin | `0x01` | `u32 length` | — |
| write | `0x02` | `u32 offset`, `u8 data[]` | — |
| commit | `0x03` | `u32 crc32`, `u32 schema_id` | — |
| abort | `0x04` | — | — |
| erase | `0x05` | — | — |
| options | `0x06` | `u8 autostart` | — |
| info | `0x07` | — | `packet_settings_project_info_response_t` |
| read | `0x08` | `u32 offset`, `u16 length` | the bytes (≤ `CONFIG_SYS_INTERFACE_RESPONSE_MAX`) |
| load | `0x09` | — | — (then restart) |

## Errors

Owners `OWNER_SYS_PROJECT_*` (`0xB2xx`), tags `ERR_PROJECT_*` (`errors/sys_error_project.h`): too big, not storing, offset, incomplete, CRC, bad frame, frame refused, schema, not stored, read range.
