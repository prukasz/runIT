# nvs_store — the board's non-volatile store

Component `components/core/nvs_store/` (in `core`: any module keeps its values here). Three parts:

- **the API** `nvs_store.h` / `nvs_store.c`, the one the modules include: named values (blobs) in namespaces of a partition (`nvs_store_*`) and a JSON file of chunks built on them (`nvs_store_json_*`). No type or name of an implementation in it: each call goes to the build's implementation through the interface;
- **the interface** `nvs_store_iface.h`, included by an implementation and by `nvs_store.c` only: the struct of functions `nvs_store_iface_t` (`init`, `open`, `get`, `set`, `erase`, `keys`) and the build's one instance of it, `nvs_store_impl`;
- **the implementation** of this chip, `esp/nvs_store_esp.c`: `nvs_store_impl` on ESP-IDF's `nvs_flash`, the one place that names it.

## Files

| File | Role |
|---|---|
| `nvs_store.h` / `nvs_store.c` | The API: `nvs_store_init`, `_open`, `_get`, `_set`, `_erase`, `_keys` (each a call of `nvs_store_impl`), the JSON file (`nvs_store_json_*`, its CRC-32); the module's error tags registered (every implementation raises them) |
| `nvs_store_iface.h` | The interface: `nvs_store_iface_t`, `extern const nvs_store_iface_t nvs_store_impl` |
| `esp/nvs_store_esp.c` | `nvs_store_impl` on ESP-IDF's `nvs_flash`: a namespace (`nvs_store_ns_t`) is its handle, "not found" is `ERR_NVS_NOT_FOUND`, any other code `ERR_ESP_ERR` |
| `errors/` | `ERR_NVS_STORE_MAP` (`MODULE_ID_NVS` 0x16, `nvs_store_id.h`), `errors_nvs_store.json` the messages |

## Implementations

An implementation includes `nvs_store_iface.h`, writes its functions as the `nvs_store_*` of `nvs_store.h` say (a key not there is `ERR_NVS_NOT_FOUND`) and defines `const nvs_store_iface_t nvs_store_impl`. The build compiles one: this chip's is `esp/`; another (a RAM one for the host tests) is a sibling folder the build takes instead.

## The store

| Call | Does |
|---|---|
| `nvs_store_init(partition)` | Makes a partition ready, at the start of the boot, once per partition: the only place that initialises or erases one. A partition the store can't use (no free pages, a new version) is erased, with all it held |
| `nvs_store_open(partition, name, &ns)` | Opens a namespace (at most 15 characters), at boot; it stays open |
| `nvs_store_get(ns, key, buf, &len)` | The value's size into `len`, and with `buf` the value (`len` in: the room); `ERR_NVS_NOT_FOUND` |
| `nvs_store_set(ns, key, data, len)` | Writes the value and commits it: after the call it survives a power cut; the old value stays until the new one is whole |
| `nvs_store_erase(ns, key)` | Erases one value (`ERR_NVS_NOT_FOUND`), or with `key` NULL every value of the namespace, and commits |
| `nvs_store_keys(ns, fn, ctx)` | Calls `fn(key, ctx)` per key of the namespace; `fn` never writes or erases in it |

Keys are at most 15 characters. Who keeps what: `board_id` (BOARD_ID.MD), `sys_actions` (SYS_ACTIONS.MD "Storage"), `sys_board`'s JSON (SYS_BOARD.MD).

## JSON files

A file is one namespace of its own, opened at boot with `nvs_store_json_open`, so another file takes only another name. The content is never parsed.

| Key | Holds |
|---|---|
| `c0`, `c1`, … | One chunk each, as `nvs_store_json_append` got it. No buffer in RAM: a chunk is written as it comes |
| `info` | `nvs_store_json_info_t`: length, CRC-32 (zlib / IEEE, `crc32_ieee`, CRC.MD) and the number of chunks |

**Append.** The offset must be the file's length, so a chunk sent twice is refused (`ERR_NVS_OFFSET`). The chunk is stored first, then `info`: a power cut leaves the file without the chunk or with it whole. A write cut short leaves a shorter file: the reader compares length and CRC with what it sent. **Replace** is erase and append; nothing keeps the old content meanwhile.

**Read.** `nvs_store_json_get` by chunk, `0..chunks-1`. A chunk must fit the reader's buffer, so a writer keeps its chunks no larger than a read can answer.

A file shares its partition (or has one of its own): when `nvs_store_init` erases the partition the file goes too.

## Errors (`MODULE_ID_NVS` 0x16)

`ERR_NVS_BAD_ARG` (an append with no data, or no chunk left), `ERR_NVS_OFFSET{got, want}`, `ERR_NVS_NO_CHUNK{chunk}`, `ERR_NVS_NOT_FOUND`. The implementation's other failures are the core's `ERR_ESP_ERR`.

## Open

- A RAM implementation for the host tests.
