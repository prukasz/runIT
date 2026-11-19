# sys_board

Spec: `SYS_BOARD.MD` (this folder).

- Every change in this folder is followed by an edit of `SYS_BOARD.MD`, if it is needed.
- The firmware runs the profile and checks nothing in it: no hash, no boot counter, no parsing of the JSON. The app compares the JSON and the record with what it has.
- `MODULE_ID_BOARD` (0x2D), the op numbers (`BOARD_OPERATIONS`: 1 JSON_APPEND, 2 JSON_ERASE, 3 JSON_INFO, 4 JSON_READ), the packed frames and the record's ID (`SYS_ACTIONS_ID_BOARD`, `sys_actions.h`) are wire format; `contract_board.json` follows every change.
- The boot only queues the RUN_ALL (`router_inject`), after the board's own devices (its GPIO) exist; a project's boot queues after it.
- The ESP32-S3's GPIO is created by `app/runit`, never by the record; the app skips its `CREATE` (`boardProfile.ts`). Keep the two in step.
- Layers: the profile builds on `sys_actions` and `sys_nvs`; nothing below names it.
