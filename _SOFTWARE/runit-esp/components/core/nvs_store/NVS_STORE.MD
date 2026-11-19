# nvs_store — the board's non-volatile store

Component `components/core/nvs_store/` (in `core`: any module keeps its values here). **The interface is one struct of functions, `nvs_store_t` (`nvs_store.h`), with no type of the implementation in it**: named values (blobs) in namespaces of a partition. The build has one store, the const `nvs_store`, which its implementation defines: this chip's is `esp/` (ESP-IDF's `nvs_flash`), the one place that names it. Another implementation (a RAM one for the host tests) defines `nvs_store` in a sibling folder and the build takes it instead. What is built on the interface (a JSON file of chunks) is `nvs_store.c`.

## Files

| File | Role |
|---|---|
| `nvs_store.h` | The interface: the store's struct `nvs_store_t` (`init`, `open`, `get`, `set`, `erase`, `keys`), the build's store `nvs_store`, and the JSON file (`nvs_store_json_*`) |
| `nvs_store.c` | The JSON file, on the interface only; its CRC-32; the module's error tags registered |
| `esp/nvs_store_esp.c` | `nvs_store` on ESP-IDF's `nvs_flash`: a namespace (`nvs_store_ns_t`) is its handle, "not found" is `ERR_NVS_NOT_FOUND`, any other code `ERR_ESP_ERR` |
| `errors/` | `ERR_NVS_STORE_MAP` (`MODULE_ID_NVS` 0x16, `nvs_store_id.h`), `errors_nvs_store.json` the messages |

## The store

Every call goes through `nvs_store`: `ERR_TRY(nvs_store.set(ns, "id", &id, sizeof(id)));`.

| Call | Does |
|---|---|
| `nvs_store.init(partition)` | Makes a partition ready, at the start of the boot, once per partition: the only place that initialises or erases one. A partition the store can't use (no free pages, a new version) is erased, with all it held |
| `nvs_store.open(partition, name, &ns)` | Opens a namespace (at most 15 characters), at boot; it stays open |
| `nvs_store.get(ns, key, buf, &len)` | The value's size into `len`, and with `buf` the value (`len` in: the room); `ERR_NVS_NOT_FOUND` |
| `nvs_store.set(ns, key, data, len)` | Writes the value and commits it: after the call it survives a power cut; the old value stays until the new one is whole |
| `nvs_store.erase(ns, key)` | Erases one value (`ERR_NVS_NOT_FOUND`), or with `key` NULL every value of the namespace, and commits |
| `nvs_store.keys(ns, fn, ctx)` | Calls `fn(key, ctx)` per key of the namespace; `fn` never writes or erases in it |

Keys are at most 15 characters. Who keeps what: `board_id` (BOARD_ID.MD), `sys_actions` (SYS_ACTIONS.MD "Storage"), `sys_board`'s JSON (SYS_BOARD.MD).

## JSON files

A file is one namespace of its own, opened at boot with `nvs_store_json_open`, so another file takes only another name. The content is never parsed.

| Key | Holds |
|---|---|
| `c0`, `c1`, … | One chunk each, as `nvs_store_json_append` got it. No buffer in RAM: a chunk is written as it comes |
| `info` | `nvs_store_json_info_t`: length, CRC-32 (zlib / IEEE, computed here) and the number of chunks |

**Append.** The offset must be the file's length, so a chunk sent twice is refused (`ERR_NVS_OFFSET`). The chunk is stored first, then `info`: a power cut leaves the file without the chunk or with it whole. A write cut short leaves a shorter file: the reader compares length and CRC with what it sent. **Replace** is erase and append; nothing keeps the old content meanwhile.

**Read.** `nvs_store_json_get` by chunk, `0..chunks-1`. A chunk must fit the reader's buffer, so a writer keeps its chunks no larger than a read can answer.

A file shares its partition (or has one of its own): when `nvs_store.init` erases the partition the file goes too.

## Errors (`MODULE_ID_NVS` 0x16)

`ERR_NVS_BAD_ARG` (an append with no data, or no chunk left), `ERR_NVS_OFFSET{got, want}`, `ERR_NVS_NO_CHUNK{chunk}`, `ERR_NVS_NOT_FOUND`. The implementation's other failures are the core's `ERR_ESP_ERR`.

## Open

- A RAM implementation for the host tests.
