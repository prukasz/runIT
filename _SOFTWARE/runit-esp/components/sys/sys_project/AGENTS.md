# sys_project

Spec: `SYS_PROJECT.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- `ROUTER_KEY_PROJECT` (0x0C), the op numbers (1 BEGIN, 2 WRITE, 3 COMMIT, 4 ABORT, 5 INFO, 6 RUN, 7 ERASE, 8 OPTIONS, 16 SECTION, 17 MARK), the error tags, and the packed layouts (`sys_project_mark_t` 9 B, `sys_project_info_t` 10 B) are wire format and stored on flash. Do not renumber or change layouts. Keep `sys_project_router.h` and `contract_project.json` in step. Add new ops at the end.
- Changing the ops, `contract_project.json`, an argument struct or the error map requires an update in `templates/`: run `sh templates/sync.sh` (regenerates `enums.json` / `errors.json`, refreshes `runit-app/generated/`, validates). Commit with `--check` green.
- Bank alternation: bank is 0 or 1. `SYS_PROJECT_ID(bank, n)` packs class PROJECT (0x03) and index `((bank + 1) << 16) | n`. `n = 0` is the program's chunk list; `n >= 1` are program chunks or staged sections. An active save fills the inactive bank (`mark.bank ^ 1`); stored project records are never touched until COMMIT.
- Commit atomicity: `commit_store` copies staged sections to their final IDs (`sys_actions_copy`), stores the chunk list, writes the project record at `SYS_ACTIONS_ID_PROJECT` (`0x03000000`, the commit point), and clears the old bank (`clear_bank`). A failed or aborted save drops all staged records with `clear_bank`.
- Section ordering: `SYS_PROJECT_SECTION_PROGRAM` (0) must be the last section in the stream (`ERR_PROJECT_ORDER`). Sections with `id != 0` must have class `<= SYS_ACTIONS_CLASS_DEVICE`.
- Size limits: each non-program section cannot exceed `sys_actions_max_bytes(id)`. Program chunks cannot exceed `CONFIG_SYS_ACTIONS_MAX_BYTES`. Total stream cannot exceed `CONFIG_SYS_PROJECT_MAX_BYTES`.
- Concurrency: `sys_project_mutex` guards the save state and shared buffers (`s_save`, `s_buf`, `s_frame`, `s_chunks`, `s_secs`).
- Execution & nesting: `sys_project_run` executes on a router task via `sys_actions_run` with `keep_going = true`. It nests three RUN levels (project record -> chunk list -> chunk); `CONFIG_SYS_ACTIONS_MAX_DEPTH` must be at least 3 (`_Static_assert`).
- Boot autostart: `sys_project_boot` queues `[ROUTER_KEY_PROJECT, SYS_PROJECT_OP_RUN]` via `router_inject` when autostart is enabled and a project is stored. It runs after devices are initialized.

