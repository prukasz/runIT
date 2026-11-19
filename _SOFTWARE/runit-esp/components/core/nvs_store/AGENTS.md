# nvs_store

Spec: `NVS_STORE.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- `nvs_store.h` holds no type, header or name of the implementation: no `nvs_handle_t`, no `esp_err_t`, no ESP-IDF include. ESP-IDF's NVS is named in `esp/` only, and no module outside this folder calls it.
- `nvs_store.c` is built on the interface only: what it needs of the chip goes through `nvs_store_*`.
- Only `nvs_store_init` initialises or erases a partition. A module never does.
- The interface holds what the modules use, no more: a new call comes with its first user.
- A file's content is opaque: nothing here parses JSON.
- `nvs_store_mutex` guards every file's `info`: both router tasks can call. Keep the order of an append: the chunk, then `info`.
- Changing the error map requires `sh templates/sync.sh`.
