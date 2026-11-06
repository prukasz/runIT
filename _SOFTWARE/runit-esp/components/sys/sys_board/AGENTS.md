# sys_board

Spec: `SYS_BOARD.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- The firmware replays the profile and checks nothing in it: no hash, no boot counter, no parsing of the JSON. The app compares the JSON with the stored record.
- `ROUTER_KEY_BOARD` (0x11), the op numbers (1 JSON_APPEND, 2 JSON_ERASE, 3 JSON_INFO, 4 JSON_READ) and the packed structs are wire format. Changing the ops or `contract_board.json` requires `sh templates/sync.sh`.
- The boot only queues the RUN_ALL (`router_inject`). It stays ahead of `sys_project_boot`, which queues after it.
- The ESP32-S3's GPIO is created by the boot step `esp32 gpio` (`runit_board_gpio_init`), never by the record; the app skips its `CREATE` (`SINGLE_TYPES` in `boardProfile.ts`). Keep the two in step.
