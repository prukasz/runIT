# data_matrix: working notes

- One table for every byte that is not a command: roles of the providers, routes between ends. Do not add a second place that decides where data go (a sink list, a binding, a listener of a channel): a route, or the role.
- The module knows no protocol. The session's frames (hello, ping, requests, answers) are `sys_session`'s; the matrix sees only a PUSH's `[tag][data]` and a raw provider's bytes. The commands are the router's; the `COMMANDS` role only hands them over (`data_matrix_commands.c`).
- A provider's end needs its role when the route is made, and `CLEAR_ROLE` removes the routes with it. A provider with no role is not attached.
- A delivery copies the matching targets under `s_lock` and sends outside it: never hold the lock over a send, an open or a callback.
- Nothing here reports an error from a delivery: a report goes to the errors stream, i.e. back into this task. A target that does not take the data loses it (`err_free`).
- The streams' ids are claimed by a symbol per id (`DATA_MATRIX_STREAMS_DEFINE`); two modules with one id must not link. A new stream is a row in its module's table, and the id is written with two hex digits.
- A module's channel tag is `SYS_SESSION_CHANNEL(MODULE_ID_x)` (from `0x80`) with the module's callback routed from `T(DATA_MATRIX_ANY, tag)`.
- Wire format (`data_matrix_end_t`, `data_matrix_route_t`, `data_matrix_role_args_t`, the commands' codes) never renumbers; `contract_data_matrix.json` and `data_matrix.h`'s enums (published by `templates/generate_enums.py`) change with it, then `templates/sync.sh`.
- Tests: `python test_host/run.py data_matrix` (from `runit-esp/`); the matrix's task and the router's run for real on fake providers.
- `stream/` (`data_stream`: the switch the matrix becomes, DATA_MATRIX.MD "data_stream") is not in the build: keep it out of `CMakeLists.txt` until the matrix is replaced; it stays a switch only: no role, session, tag, format, queue or task, and its port contract is written in `data_stream.h`.
