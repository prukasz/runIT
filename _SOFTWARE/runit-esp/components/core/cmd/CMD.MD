# cmd — shared command contract (for agents)

Component `components/core/cmd/`. Holds the transport- and routing-agnostic command contract shared between `router` (`components/core/router/`) and `sys_device` / contracts / device drivers:
- Frame limits (`CMD_FRAME_MAX`, `CMD_HEAD`, `CMD_DATA_MAX`), the header of every frame (`cmd_hdr_t`: its type byte `CMD_TYPE_*`, the kinds `cmd_kind_e`, the sources `CMD_SRC_*`) and the send flags (`CMD_SEND_URGENT`, `CMD_SEND_SPARE`, `CMD_SEND_NOWAIT`, in `utils`' `cmd_types.h`)
- Handler type and direction (`cmd_f`, `CMD_F_PARAMS`, `cmd_dir_e`; the handler's `cmd_in_t` and `cmd_out_t` are `utils`' `cmd_types.h`)
- `static inline` answer helpers (`cmd_reply`, `cmd_append`)
- Op-table and payload-parsing macros (`cmd_table.h`: `cmd_none_t`, `CMD_TABLE_DECLARE`, `CMD_REPLY`, `CMD_HANDLER`, `CMD_HANDLER_TAIL`)
- Error module `MODULE_ID_CMD` (`0x27`): `ERR_CMD_BAD_LEN`, `ERR_CMD_OUT_FULL`, `ERR_CMD_BAD_ARG`, `ERR_CMD_DIRECTION`

## Files

| File | Role |
|---|---|
| `cmd.h` | Frame limits, the header (below), `cmd_f` (over `cmd_in_t` / `cmd_out_t`, `cmd_types.h` in `utils`, which also holds the `CMD_SEND_*` flags), `cmd_dir_e`, `static inline` `cmd_reply` / `cmd_append`, `cmd_push_head` (a PUSH packet's header written into the `CMD_HEAD` bytes a stream module leaves before its data: the one way every stream port writes it) |
| `cmd_table.h` | Op-table declarations (`CMD_TABLE_DECLARE`) and handler wrappers (`CMD_HANDLER`, `CMD_HANDLER_TAIL`, `CMD_REPLY`) |
| `errors/cmd_errors.h`, `errors/errors_cmd.json` | `ERR_CMD_MAP` (`MODULE_ID_CMD = 0x27`) and user messages |
| `cmd_id.h` | `MODULE_ID_CMD` |
| `cmd.c` | Registers `ERR_CMD_MAP` under `MODULE_ID_CMD` (`0x27`, `ERR_MODULE_DEFINE`), linked `WHOLE_ARCHIVE` |
| `host/` | `test_cmd.c` and `host_test.json` (`python test_host/run.py cmd`) |

## The header

Every frame on a provider's line and every packet in the data matrix (DATA_MATRIX.MD) starts with `cmd_hdr_t`, `CMD_HEAD` = 5 bytes, packed, little-endian: the same bytes on the line and in the matrix. Wire format, published to the app (`enums.json`: groups `cmd_frame`, `cmd_type`, `cmd_src`, the enum `cmd_kind_e`).

| Field | Meaning |
|---|---|
| `type u8` | bits 0-1 the router's flags (`ROUTER_FLAG_URGENT`, `_NOREPLY`, `CMD_TYPE_FLAGS`), bit 2 an answer's status (`router_status_e`, `CMD_TYPE_STATUS`), bits 3-5 zero, bits 6-7 the kind (`cmd_kind_e`: `CMD_REQUEST`, `CMD_ANSWER`, `CMD_PUSH`, `CMD_CONTROL`; `CMD_TYPE(kind)`, `CMD_KIND_OF(type)`) |
| `src u8` | who made it (`CMD_SRC_*`) |
| `key u8` | a module's number: a request's router key, an answer's request's, a PUSH's stream, `MODULE_ID_SESSION` for CONTROL |
| `seq u16` | a request's sender's counter, echoed by its answer; a PUSH's count of its class on its link; 0 for CONTROL |

**Sources** (`CMD_SRC_*`): `0x00` none (a board with no id given, a frame no session has put on a line yet); boards `0x01..0xEF` (a board's place in the project, set by a command, kept in NVS); clients `0xF0..0xFC` (the app, a test host: each picks its own); `0xFE` `CMD_SRC_SELF` (the board's own commands, `router_inject`) and `0xFF` `CMD_SRC_ALL` (a listener's mark for every peer) are never on a line. `src` is set once: the session that puts a frame on a line writes its board's id when `src` is 0, nothing else touches it, so `src` and `key` go end to end and `seq` is per link.
