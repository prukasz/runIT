# cmd

Spec: `CMD.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- Do not add references to `legacy docs` unless the user asks for them.
- `cmd_f` is the common command handler signature used by both the router (key tables) and the device registry (op tables). It carries no transport, key-routing or registry logic.
- Error tags in `ERR_CMD_MAP` (`ERR_CMD_BAD_LEN`, `ERR_CMD_OUT_FULL`, `ERR_CMD_BAD_ARG`) are wire format (`ERR_MOD_CMD` = 11). Do not renumber them. Changing them requires `sh templates/sync.sh` (regenerates `errors.json`, refreshes `generated/`, validates). Commit with `--check` green.
- Response buffer handling:
  - `cmd_reply` writes a contiguous response and sets `*out_len`; returns `ERR_CMD_OUT_FULL` when `len > out_cap`.
  - `cmd_append` writes sequential chunks into `out` advancing `*pos`; returns `ERR_CMD_OUT_FULL` when `*pos + len > cap`.
  - Leaving `out_len` untouched signifies an empty response.
- Argument validation: `cmd_name_arg` enforces strict NUL-termination with no trailing bytes after the NUL character (`ERR_CMD_BAD_ARG`).
- Linker requirements: `CMakeLists.txt` must retain `WHOLE_ARCHIVE`. Error tags are registered by a constructor in `cmd.c`. A handler may raise `ERR_CMD_*` without calling `cmd_reply`, so the linker must never discard `cmd.c`.
- Decoupling invariant: `cmd` must not depend on `router` or `sys_device`. It depends only on `errors` and `utils`.

