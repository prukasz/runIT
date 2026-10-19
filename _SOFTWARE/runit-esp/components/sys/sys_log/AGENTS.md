# sys_log

Spec: `SYS_LOG.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- `ROUTER_KEY_LOG` (0x08), the op numbers (1 SET, 2 GET, 3 ERRORS), the error tag and the layout of `sys_log_cfg_t` are wire format. The layout is also stored: the app keeps the SET frame as the record `SYS_ACTIONS_ID_LOG`, so a change breaks records already saved. Add a new option at the end and keep the old ones.
- Changing the ops, `sys_log_cfg_t`, the error map or `contract_log.json` requires an update in `templates/`: run `sh templates/sync.sh` (regenerates `enums.json` / `errors.json`, refreshes `runit-app/generated/`, validates). Commit with `--check` green.
- The hook never sends a line from an ISR, before the scheduler runs, or from the connector task. Those three conditions stay: each one prevents a loop or a crash.
- The hook sends with `sys_connector_send` and drops a line on a full queue. It never reports an error: a report would log, and logging is the hook.
- A new level or flag is checked in `sys_log_set`, the one place that checks and applies options.
