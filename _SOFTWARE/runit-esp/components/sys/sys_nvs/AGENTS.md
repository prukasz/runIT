# sys_nvs

Spec: `SYS_NVS.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- Only `sys_nvs_init` calls `nvs_flash_init_partition` or erases a partition. A module never does.
- A file's content is opaque: nothing here parses JSON.
- `sys_nvs_mutex` guards every file's `info`: both router tasks can call. Keep the order of an append: the chunk, then `info`, then the commit.
- Changing the error map requires `sh templates/sync.sh`.
