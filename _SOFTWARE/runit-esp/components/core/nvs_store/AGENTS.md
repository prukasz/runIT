# nvs_store

Spec: `NVS_STORE.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- Three parts, kept apart: the API (`nvs_store.h` / `.c`, what the modules include), the interface (`nvs_store_iface.h`, included by an implementation and `nvs_store.c` only), the implementation (`esp/`). The API and the interface hold no type, header or name of an implementation: no `nvs_handle_t`, no `esp_err_t`, no ESP-IDF include. ESP-IDF's NVS is named in `esp/` only, and no module outside this folder calls it.
- `nvs_store.c` calls the implementation only through `nvs_store_impl`; the JSON file only through `nvs_store_*`.
- Only `nvs_store_init` initialises or erases a partition. A module never does.
- The interface holds what the modules use, no more: a new function comes with its first user, in the API and the interface together.
- A file's content is opaque: nothing here parses JSON.
- `nvs_store_mutex` guards every file's `info`: both router tasks can call. Keep the order of an append: the chunk, then `info`.
- Changing the error map requires `sh templates/sync.sh`.
