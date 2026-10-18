# errors

Spec: `ERRORS.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- Module IDs (`ERR_MOD_*`) are assigned only in `err_modules.h`. They must be unique firmware-wide and never renumbered. `ERR_MOD_CORE` is 0.
- Error tags: `ERR_TAG(module, code)` packs module (bits 16..31) and code (bits 0..15). Tag 0 represents success / no error. Error codes are wire format; do not renumber them.
- Tag maps and registration:
  - Each module declares its tags via `ERR_TAGS_DECLARE(MAP)` in its header.
  - The module registers the table once via `ERR_MODULE_DEFINE(mod_id, name, MAP)` in a `.c` file that is guaranteed to be linked.
  - Every tag has its user message (and, when the user can act on it, a `hint`: what to do) in `errors_<module>.json` next to the header of its map (templates/AGENTS.md "Errors"); a new module has a new file. Changing an error map or adding a tag requires updating it and `templates/`: run `sh templates/sync.sh` (runs `generate_errors.py`, refreshes `generated/`, validates). Commit with `--check` green.
- Node lifecycle and chain ownership:
  - An error is a linked chain of nodes (`err_h` = pointer to outermost node). `NULL` strictly indicates success. Never return `NULL` for a failure.
  - Nodes are allocated from a static fixed block pool (`err_pool_alloc` / `err_pool_free`), task- and ISR-safe under a spinlock critical section.
  - Double-free detection is enforced by a bitmask; double-freeing asserts.
  - Never share cause nodes across chains. Cyclical chains are bounded by `ERRORS_POOL_BLOCKS + 1`.
  - Whoever terminates a chain owns it and must free it via `err_free(chain)` or `err_report` (which passes it to the sink and frees it). `err_log` only borrows.
- Pool exhaustion behavior:
  - `ERR_NEW` returns the static `ERR_POOL_EMPTY` node when the pool is exhausted.
  - `ERR_WRAP` returns `cause` unchanged when the pool is exhausted.
  - Therefore, node severity is passed into the constructor and never mutated on the returned node.
- Severity calculation:
  - `err_chain_sev(chain)` walks from the outermost node inward. Nodes with default severity contribute their tag's default severity (highest wins).
  - The first node encountered with an explicitly set severity contributes that severity and stops the walk, acting as that level's final verdict on deeper causes.
- Reporting and Sinks:
  - `err_report` / `err_report_ex` logs the chain, forwards it to the registered `err_sink_f` (set at boot by the application), and frees the chain.
  - Code on the reporting path (connector task, logging capture) must never call `err_report` (to avoid recursion loops); use `err_log` and `err_free` instead.
- Wire serialization:
  - `err_encode` formats a chain as: `[count u8, bit 7 = truncated]` followed by per-node `[tag u32 LE][sev u8][len u8][payload len B]`.

