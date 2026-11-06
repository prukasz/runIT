# sys_ota — firmware update

Component `components/sys/sys_ota/`. A new firmware written into the other app slot over any link, the same as every other command: `BEGIN`, `WRITE` in order, `COMMIT`, then a restart boots it. `esp_ota_*` of ESP-IDF underneath.

| File | Role |
|---|---|
| `sys_ota.h` / `sys_ota.c` | The write, the guard, confirm, info |
| `router/sys_ota_router.h/.c` | `ota` router (`ROUTER_KEY_OTA` = 0x12). The header publishes every op (`SYS_OTA_OP_*`, with its sides) and the wire layouts, each op above its frame |
| `router/contract_ota.json` | Contract descriptor of the router |
| `errors/` | `ERR_SYS_OTA_MAP`, the messages |

## Slots

Two app slots of 4 MB, `ota_0` and `ota_1`, and `otadata` (`partitions.csv`). Slots of this size stay ahead of the firmware: the partition table only changes by cable. Cable flashing goes on working: `idf.py flash` writes `ota_0` and resets `otadata`, so the board boots it.

## An update

1. The app stops the program. `BEGIN` refuses while the guard says busy (`ERR_OTA_BUSY`): `runit` gives it `vm_exec_mode() != VM_RUN_STOPPED`, since a flash write stalls what runs from flash. Restart first or stop the VM, either works.
2. `BEGIN {size}` opens the write, in place of one left open by a client that went away. It erases as it goes (`OTA_WITH_SEQUENTIAL_WRITES`), so no command blocks the work task for the whole slot.
3. `WRITE {offset} + data`. The offset must be the bytes so far (`ERR_OTA_OFFSET`).
4. `COMMIT` checks the length and the image's own SHA-256 and chip (`ERR_OTA_BAD_IMAGE`) and sets the boot slot. The running firmware goes on: the app sends SYS RESTART, so the answer leaves first.

Each op is a handler's own flash I/O (CMD.MD "Long work"): the client drives it chunk by chunk, so it needs no job.

The app: `writeFirmware` in `runit-app/src/backend/session/ota.ts`, the card Firmware on the Project screen (`FirmwareCard.tsx`).

## Rollback

`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` is on. A new firmware's first boot is `SYS_OTA_STATE_PENDING`: if it is not confirmed, the next restart goes back to the old slot. The app sends `CONFIRM` once the session's hello has passed, so a firmware whose link does not work can't lock the board out: it is restarted (by hand, or a crash) into the old one. A firmware flashed by cable is `SYS_OTA_STATE_UNKNOWN` and has nothing to confirm. While a firmware is pending no new update starts (the flash's `ESP_ERR_OTA_ROLLBACK_INVALID_STATE`): confirm it first.

## Remote

| Op | Side | Args | Answer |
|---|---|---|---|
| `0x01` BEGIN | W | `u32 size` | – |
| `0x02` WRITE | W | `u32 offset`, data | – |
| `0x03` COMMIT | W | – | – |
| `0x04` ABORT | W | – | – |
| `0x05` INFO | R | – | `u8 slot, u8 state (sys_ota_state_e), u8 open, u32 written, u32 slot_bytes` |
| `0x06` CONFIRM | W | – | – |

## Errors (`ERR_MOD_SYS_OTA` = 23)

`ERR_OTA_BUSY`, `ERR_OTA_NOT_OPEN`, `ERR_OTA_TOO_BIG {len, max}`, `ERR_OTA_OFFSET {got, want}`, `ERR_OTA_INCOMPLETE {got, want}`, `ERR_OTA_BAD_IMAGE`; the flash's own errors pass through `ERR_ESP`.

## Open

- No signature: anyone in BLE range can write a firmware (`CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT` before a release).
- The hello's firmware version is sent as 0 (SYS_SESSION.MD), so the app can't yet compare the descriptors it bundles with what runs.
