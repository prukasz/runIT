# errors

What the module is and how it is used: `ERRORS.MD` (this folder). This file holds only the rules for changing it.

## Never break

- The node is opaque: `struct err` lives in `internal/err_internal.h` only, never in a public header. Outside this folder a node is read with `err_tag` / `err_cause` / `err_data` / `err_find`, made with `ERR_NEW` / `ERR_WRAP` and never changed.
- Every walk along a chain, here and in any module, is bounded by `ERR_CHAIN_MAX`: never a copy of its own.
- `errors` depends on `utils` and libc only: no cmd, router, session or transport header. A command is `cmd_req_t` from `utils` (`cmd_types.h`), never a router type; `errors` raises its own tags, never `ERR_CMD_*`.
- Module numbers and tag codes are wire format: never renumbered. A new tag goes at the end of its map with the next code; a module's number is its `MODULE_ID_DECLARE` in its `<module>_id.h`.
- No allocation outside the static pool; nothing here blocks or waits (the pool is safe from ISRs).
- `ERR_HDR_LEN` is sized by `sizeof(void*)` and checked against the node (`err_internal.h`), so the host build with 8-byte pointers stays true.

## A change here also changes

- A tag added or changed (here or in any module): its message in the `errors_<module>.json` next to its map, then `sh templates/sync.sh` (fails on a tag without a message).
- The error record's layout (`err_record.h`, `ERR_RECORD_*`): `sh templates/sync.sh`, the app's decoder (`runit-app/src/backend/protocol/errors.ts`) and `.claude/skills/runit-esp/scripts/router_link.py`, in the same change.
- Any behaviour: `ERRORS.MD`, and the `errors.md` checklist of the `runit-esp` skill when a rule for every module changes.

## Tests

`python test_host/run.py errors` from `runit-esp/`, all three variants (`info`, `no_info`, `no_assert`). A new behaviour gets its test first; what a misuse does with asserts off goes in the `#ifdef NDEBUG` part. No test waits: no sleep, no delay; threads are joined.
