# cmd

Spec: `CMD.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- `cmd_f` (`ctx`, `cmd_in_t in`, `cmd_out_t* out`) is the common command handler signature used by both the router (key tables) and the device registry (op tables). It carries no transport, key-routing or registry logic.
- `cmd_dir_e` (`CMD_DIR_READ` = 0, `CMD_DIR_WRITE` = 1) is wire format (the byte after the root key, published in `enums.json`): do not renumber it. `cmd_side` is the one place a side is picked (CMD.MD "Read and write").
- `CMD_FRAME_MAX` and `CMD_HEAD` are the one frame size (CMD.MD "Frame size"), published to the app (group `cmd_frame`, `enums.json`): no module keeps a frame size of its own, every buffer and limit derives from them. Changing them requires `sh templates/sync.sh`.
- Error tags in `ERR_CMD_MAP` (`ERR_CMD_BAD_LEN`, `ERR_CMD_OUT_FULL`, `ERR_CMD_BAD_ARG`, `ERR_CMD_DIRECTION`) are wire format (`ERR_MOD_CMD` = 11). Do not renumber them. Changing them requires `sh templates/sync.sh` (regenerates `errors.json`, refreshes `runit-app/generated/`, validates). Commit with `--check` green.
- The answer:
  - `cmd_reply` writes the whole answer and sets `out->len`; `ERR_CMD_OUT_FULL` when `len > out->cap`.
  - `cmd_append` writes after `out->len` and advances it; `ERR_CMD_OUT_FULL` past `out->cap`.
  - `out->len` 0 is an empty answer. A command answers once; longer data is paged by the command (CMD.MD "The handler").
- Linker requirements: `CMakeLists.txt` must retain `WHOLE_ARCHIVE`. Error tags are registered by a constructor in `cmd.c`. A handler may raise `ERR_CMD_*` without calling `cmd_reply`, so the linker must never discard `cmd.c`.
- Decoupling invariant: `cmd` must not depend on `router` or `sys_device`. It depends only on `errors` and `utils`.
