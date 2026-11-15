# cmd — shared command contract (for agents)

Component `components/core/cmd/`. Holds the transport- and routing-agnostic command contract shared between `router` (`components/core/router/`) and `sys_device` / contracts / device drivers:
- Frame limits (`CMD_FRAME_MAX`, `CMD_HEAD`, `CMD_DATA_MAX`) and shared TX queue flags (`CMD_SEND_URGENT`, `CMD_SEND_SPARE`, `CMD_SEND_NOWAIT`)
- Handler types and direction (`cmd_in_t`, `cmd_out_t`, `cmd_f`, `CMD_F_PARAMS`, `cmd_dir_e`)
- `static inline` answer helpers (`cmd_reply`, `cmd_append`)
- Op-table and payload-parsing macros (`cmd_table.h`: `cmd_none_t`, `CMD_TABLE_DECLARE`, `CMD_REPLY`, `CMD_HANDLER`, `CMD_HANDLER_TAIL`)
- Error module `ERR_MOD_CMD` (`MODULE_ID_CMD = 0x27`): `ERR_CMD_BAD_LEN`, `ERR_CMD_OUT_FULL`, `ERR_CMD_BAD_ARG`, `ERR_CMD_DIRECTION`

## Files

| File | Role |
|---|---|
| `cmd.h` | Frame limits, shared `CMD_SEND_*` TX flags, `cmd_in_t`, `cmd_out_t`, `cmd_f`, `cmd_dir_e`, `static inline` `cmd_reply` / `cmd_append` |
| `cmd_table.h` | Op-table declarations (`CMD_TABLE_DECLARE`) and handler wrappers (`CMD_HANDLER`, `CMD_HANDLER_TAIL`, `CMD_REPLY`) |
| `errors/cmd_errors.h`, `errors/errors_cmd.json` | `ERR_CMD_MAP` (`ERR_MOD_CMD = 0x27`) and user messages |
| `cmd.c` | Registers `ERR_CMD_MAP` (`ERR_MODULE_DEFINE`) and claims `MODULE_ID_CMD` (`0x27`), linked `WHOLE_ARCHIVE` |
| `host/` | `test_cmd.c` and `host_test.json` (`python test_host/run.py cmd`) |
