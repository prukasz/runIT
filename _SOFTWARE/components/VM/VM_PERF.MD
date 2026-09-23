# VM Performance History

Measurements and optimisation decisions for the VM's hot path (accessor resolution, pin reads
and writes, the expression interpreter). **History, not a live suite:** the harness that
produced these numbers (`components/runit/vm_bench.c`) was deleted in commit `47207d3`, and the
code has changed since (EN became a list, blocks gained verify functions). Treat every figure
as a baseline to re-measure, not a current fact. What stays valid is the reasoning: which
shapes are cached, why reads are out of line and writes inline, why the component builds at
`-O2`, and which experiments were tried and reverted.

Measured at 160 MHz, best of 3 trials, `esp_cpu_get_cycle_count()`.

## Access benchmark (historical)

`components/runit/vm_bench.c` times the access layer against the jagged 2D shape — an 8×8 float grid (`VM_OBJ_PTR[8]` of `VM_OBJ_F[8]`), read cell by cell. Enable with `RUNIT_ENABLE_VM_BENCH`; keep `RUNIT_ENABLE_VM_SELFTEST` **off** while benchmarking, since both drive the shared store and the test leaves it detached.

| Scenario | Shape | What it adds over the row above |
| :--- | :--- | :--- |
| raw C pointer walk | — | the floor: the data walk with no VM |
| `flat[i]` | 1 literal index | one resolve: id bounds check, header read, offset shift |
| `grid[r][c]` | 2 literal indices | one `VM_OBJ_PTR` dereference level |
| `grid[rsel][csel]` | 2 by-ref indices | two nested resolves per access |
| `grid["rN"][c]` | name + literal | a tag scan of the parent's children |
| row resolved once, payload walk | `vm_get_obj` + `vm_payload_get_at` | **removes** 56 of the 64 resolves |
| `grid[r][c]` write | 2 literal indices | the `mutable` check and the `upd` flag |

Each scenario performs exactly 64 accesses per pass and returns their sum, checked against 2016 before timing — a scenario that silently stops resolving would otherwise look extremely fast. Timing is `esp_cpu_get_cycle_count()` over 500 passes, best of 3 trials, with the CPU frequency measured rather than assumed. Interrupts stay live (BLE, timers), which is what "best of 3" is for: a trial that catches an ISR is discarded rather than averaged in.

The rows are ordered so each one isolates a single added cost. The last two are the ones to design against: **the payload walk is the shape a fold block should use** (resolve the row once, step its elements), and by-name access is the expensive one — right for a parsed message, wrong for a compiled path where the position is already known.

One assertion is worth knowing about: stage K asserts that `msg["temp"]` **without** a trailing `[0]` yields 0 rather than the value, pinning the documented two-step rule so a future auto-dereference change cannot silently pass.

### What the hot path is allowed to cost

The first benchmark run put `flat[i]` at 111 cyc against a 5.2 cyc raw walk. Disassembly, not guesswork, said where it went — and none of it was the walk itself:

* **A call.** `VM_OBJ_SCALAR_GET` invoked an out-of-line `vm_obj_get_payload`, so every pin read paid `entry`/`retw` register-window rotation and returned its result by writing 12 bytes of `vm_payload_t` through a pointer that the caller read straight back.
* **Two dispatches.** `vm_payload_get_val()` switched on the stored type into a `vm_val_t`, spilled it to the stack, and `VM_INTERNAL_LOAD_SCALAR()` switched on the same type again to load it back.
* **Two flash data loads.** `vm_obj_type_sizes[]` for the width and `vm_obj_type_shifts[]` for the count — `.rodata` lives in flash, so both went through the data cache on every element access.
* **A `mull`.** The count came from a shift, then the address came from a multiply, to arrive at a number the shift already had.

The fixes, in the order they matter:

1. **`vm_resolve_fast()` moved into the header** (`__always_inline`). The three literal shapes resolve as straight-line code with the type and pointer live in registers. No call, no window rotation, no struct round-trip.
2. **`VM_LOAD_CAST_TO()` fuses load and convert** into one dispatch, replacing read-then-cast. `vm_payload_get_val()` remains for callers that hold a value rather than an address.
3. **`vm_obj_elem_ptr()` works in byte offsets.** `off = i << shift` is both the address and — compared against `payload_size` — the bounds check. No count, no width lookup, no multiply.
4. **Shift table lookup (`vm_obj_type_shifts[]`).** Fast log2 bitshift yields element offsets without division; widened to 16 entries so the lookup is total over the 4-bit `obj_t` field.

A float read of `flat[i]` now ends in a single `lsi f0, a11, 0`, and the binary got *smaller* despite the inlining, because a fused switch is less code than two.

### Expression blocks

Three programs of rising size on the same machine, timed per **block execution** rather than per access — the whole of what a pass pays: trigger scan, `custom_data` check, bytecode walk, publish, ENO write. All eight pins are cached single-literal accessors on a dedicated input object, and none of the three uses a transcendental, so what is measured is the interpreter rather than libm.

| Program | cyc/exec | µs @ 160 MHz |
| :--- | ---: | ---: |
| 2 pins, 1 op | 473 | 2.98 |
| 4 pins, 3 ops | 793 | 4.99 |
| 8 pins, 20 ops + 6 literals | 2832 | 17.81 |

The three points fit **~233 cycles fixed + ~78 cycles per bytecode instruction**, and the per-instruction figure is flat across the two intervals (80 and 76) — a pin read, a literal push and an arithmetic op all cost about the same. Since a cached pin read is 27 cyc on its own and a float add is about two, that flatness says the per-instruction overhead, not the work, is what is being measured.

### Where the 78 cycles is not

Two plausible culprits were checked against the disassembly and **both are innocent**, which is worth recording so they are not chased again:

* **The opcode switch is not a linear compare chain.** GCC emits a *binary decision tree* — `bnei 5`, `bgeui 6`, `bnei 2`, `bgeui 3` … — so dispatch is about six branches. It is not a jump table (this target emits only 15 `jx` in the whole firmware, so the Xtensa cost model appears to avoid them), but a six-branch tree is 10–20 cycles, not 78.
* **The inlined fault paths are not the cost.** Every guard building its own error puts 70 `SE_alloc_bytes` + `vm_block_report_error` pairs in the function, at 2569 instructions. Hoisting them into one epilogue was tried: it collapsed those to 5 call sites and cut **3.2 kB** from the image, and it made every program **4–6% slower** — 473 → 502 cyc on the two-pin add, 2832 → 2945 on the twenty-op case. It was reverted.

### Where it is

Register pressure. The loop body reloads its variables from the frame every instruction — `l8ui a2, a1, 112`, `l8ui a10, a1, 134` — because Xtensa's windowed ABI leaves this function about twelve usable registers while the loop wants `pc`, `sp`, `loaded`, the code pointer and both struct pointers live at once, on top of a 128-byte frame for `st[16]` and `in[16]`.

That also explains the failed experiment exactly: an epilogue must be told where it went wrong, so `at`, `op`, the reason and the `err_h` all become live across the whole loop — four more spilled values, paid once per *instruction* instead of once per *fault*. The lesson generalises: in this loop, anything that adds a live value loses, and code size is not the currency.

The untried lever is the classic one for a stack interpreter — keep the top of stack in a local instead of in `st[sp-1]`, so a binary op costs one load rather than two loads and a store — with dropping the `in[]` pin cache (which buys a repeat mention 27 cyc but costs a live register on every instruction) as the cheaper half-measure.

That took `flat[i]` 111 → 66 cyc and `grid[r][c]` 141 → 105. It also made every *other* row slightly worse — by-ref 500 → 512, by-name 665 → 686, write 152 → 157 — because a shape the inline resolver does not handle now pays for the attempt before walking. That is the intended trade, but it exposed where the remaining cost actually was:

5. **A by-ref index no longer recurses when it does not have to.** `resolve_d()` called itself for every `VM_IDX_REF` step — a function with a 160-byte frame, re-entered twice per `grid[rsel][csel]` access, to read one `uint8_t` selector. It now tries `vm_resolve_fast()` on the sub-accessor first; only a selector that is itself chained or by-ref falls through to the recursion.

   **The depth cap has to be tested before that, not after.** `VM_ACCESSOR_MAX_DEPTH` is enforced by counting `resolve_d()` frames, and the fast path resolves a level without one — so letting it take the *last* level of a chain made `MAX_DEPTH + 1` fit inside `MAX_DEPTH` frames and pass. Stage C caught it: "one level past MAX_DEPTH is refused" failed while "exactly at MAX_DEPTH still resolves" passed, which is the signature of swallowing precisely one level. The fix declines the fast path at the limit and hands that level to `resolve_d()`, which refuses it and produces the same error trace as before. Worth remembering if any other shortcut is ever added below a bounded walk: a cap counted in frames breaks the moment something resolves without one.
6. **`VM_IDX_NAME` carries its length.** `find_child_by_name()` ran `strlen()` on the same constant string on every access. `vm_index_t` had padding after `kind`, so `name_len` costs nothing in size and is measured once at build. Use `VM_IDX_BY_NAME("temp")` for static initialisers — a hand-written `{.kind = VM_IDX_NAME, .name = "temp"}` leaves the length zero and matches **nothing**.
7. **Writes are inline too.** `vm_store_inline()` moved to the header, so `VM_OBJ_SET_SCALAR` and `VM_OBJ_SET_SCALAR_AT_IDX` resolve and store without a call — the latter matters most, since it is how every block publishes a result *and* what `vm_block_set_ENO()` runs for every block on every cycle. Only the error arms (`vm_obj_not_scalar_err`, `vm_obj_not_mutable_err`, `vm_obj_oob_err`, `vm_obj_null_obj_err`) stayed in the `.c`, where there is an ambient `OWNER` to build them with.
8. **`vm_get_obj()` uses the fast path** — it went straight to the walk, which cost the fold-block shape a full resolve per row.

9. **Read conversion split back out of line.** Fusing load-and-convert was right for speed and wrong for size: six arms — including a float→int64 saturation path and a U64→float conversion that drags in `__floatundisf` — measured ~450 B at *every* pin. One 2-input block came to 1.8 kB, and a 50-type palette to ~90 kB. Dispatching on the **destination** instead means only three shapes exist (float, ≤32-bit integer, 64-bit integer), so `vm_get_as_f32` / `_i32` / `_i64` — 370 B total, one copy — serve every pin in the program. `vm_resolve_fast()` stays inline, which is where the speed came from.

| symbol | before | after |
| :--- | ---: | ---: |
| one read site (`bench_flat_literal`) | 424 B | **252 B** |
| `vm_block_is_enabled` | 518 B | **300 B** |
| a 2-in/1-out block type | 1803 B | **1395 B** |
| one write site (`bench_grid_write`) | 543 B | 543 B |

Writes were deliberately left inlined. They are fewer than reads, they were the slower path, and the ~400 B they cost per site buys back the call — the right trade while flash is the plentiful resource and RAM is not.

> These figures were measured **before** EN became a list. `vm_block_is_enabled()` is now a loop over `en_cnt` sources rather than a single read, so its 300 B is stale in a direction not yet measured; the other rows are unaffected.

**Array iteration inverts the trade, and one round of measurement proved it.** `VM_PAYLOAD_GET_VAL` was routed through the shared converters along with everything else, and the payload walk went 21.7 → 39.1 cyc/element — 80% slower. A pin read runs its conversion once per block per cycle, so trading ~10 cyc for ~200 B across hundreds of sites is clearly right. A fold block runs it *per element*, with the type loop-invariant, across a handful of sites. So `VM_OBJ_SCALAR_GET` calls (`VM_LOAD_CAST_TO`) and `VM_PAYLOAD_GET_VAL` inlines (`VM_LOAD_CAST_TO_FAST`) — reach for the second only inside a loop over elements.

10. **Resolution cache, tier 1 only.** An accessor whose *address cannot move* is pre-resolved once by `vm_accessor_cache_build()` and thereafter costs four loads instead of a walk. Two shapes qualify: a whole-object accessor, and a single literal index on a root object.

    **Why those two and nothing deeper.** Neither traverses a `VM_OBJ_PTR`, so nothing a running program does can invalidate them — no generation counter, no invalidation, and no pathological case. A two-level chain is deliberately excluded: its address depends on a link `vm_obj_link()` can move underneath it.

    A single literal index on a *pointer* object is cached, and safely so: what is cached is the address of the pointer **slot**, not the object behind it. `vm_get_obj()` still dereferences the link live, so re-linking is picked up exactly as before. That is the whole reason this tier needs no invalidation machinery.

    The loader calls it at the end of `vm_loader_add_accessor()`, where objects already exist. Code that builds accessors directly must call it; **skipping it costs speed and nothing else** — the cache is never a correctness input, and an accessor that was never built simply resolves the long way. `vm_loader_reset()` reclaims the arena, so a reload starts every accessor uncached.

    Cost: `vm_accessor_t` grows 8 → 20 bytes. For a 100-block/500-pin program that is **+6 kB**, against a ~19 kB program total.

    Measured, at 160 MHz:

| scenario | before | after | |
| :--- | ---: | ---: | ---: |
| `flat[i]` | 76.5 | **34.4** | −55% |
| payload walk | 21.7 | **16.8** | −23% |
| `block_add` scalar | 257.9 | **156.6** | −39% |
| `grid[r][c]` | 105.6 | 113.6 | +8% |
| `grid[r][c]` write | 138.7 | 147.6 | +6% |
| by-name | 659.5 | 672.3 | +2% |

    **The uncached shapes pay ~8 cyc for a flag test they always fail.** That is the same trade the inline fast path made and it is worth it here — a `block_add` on cached pins dropped 100 cycles — but it means the two-level chain `grid[r][c]` is now the most expensive *common* shape, since it is both uncacheable and taxed. If array-of-structs wiring turns out to dominate real programs, the fix is not a generation counter but static proof: the compiler knows whether any switch block writes a given cell, so a chain through a cell nothing re-links could be marked cacheable at upload time and keep the invalidation-free property.

    Stage L pins the property the tier rests on — that the cache is *invisible*. A cached accessor and an identical uncached one must agree on every answer including failures; a cached write must still honour `mutable`; the excluded shapes must refuse to cache; and a rebuild whose object has gone must clear the flag rather than leave a stale address behind it.

11. **The tag scan called `memcmp()` — as a library call.** `find_child_by_name()` filtered on length and first character, then handed the rest to `memcmp(tag + 1, name + 1, n - 1)`. The length is not a compile-time constant, so GCC emitted a real call: `call8` + `entry` + generic prologue + `retw`, to compare **one byte** for a two-character tag. Every child that shared a prefix paid it, which parsed message fields routinely do (`temp`/`time`, `val1`/`val2`).

    The arithmetic pinned it before the fix: by-ref and by-name run the identical two-level shape through `resolve_d` and differed by 672 − 405 = 267 cyc, almost exactly 4.5 candidates × one call-plus-`memcmp`. An open-coded byte loop replaces it — a tag is at most `VM_OBJ_NAME_MAX`, so there is nothing a library call can do better.

12. **`resolve_d`'s cold arms moved out of its body.** Eight `SE_ERR_NEW` sites had pulled `strlen` and a pile of payload construction into the walk. They are now `noinline` helpers; `SE_ERR_NEW` records no file or line, so the trace is unchanged — the tag, the ambient `OWNER` and the payload fields carry all of it. `resolve_d` went 1306 → 1198 bytes.

    **The stack frame did not move: still 144 bytes.** That was the stated reason for doing this and the prediction was wrong — the frame is driven by the recursion's own locals and inlined struct returns, not by error payloads. Worst-case depth cost is ~1.15 kB, essentially where it started, so **the untrusted-input stack item remains open**. Closing it properly means converting the by-ref recursion into an iterative walk over a fixed-size stack, bounded by construction rather than by a depth constant.

    A second attempt at the frame — taking all of `payload_as_index()` out of line — was measured and reverted: it cost a by-ref access two calls (**405 → 418 cyc**), left the frame at 144 anyway, and its only real benefit was keeping `roundf`/`__fixsfdi` out of the walk. Only the float arm (`index_from_float`) is out of line now; the integer arms are a load and a cast and belong inline. Worth remembering as the pattern: evict the arm that drags a library in, not the function that contains it.

**Considered and rejected:** a `flags` byte on `vm_accessor_t` classifying the shape at load time, so `vm_resolve_fast()` could skip its kind checks and slow shapes could skip the attempt. It is worth ~5 cyc, and an opt-in bit means any accessor built by hand rather than through `vm_accessor_create()` silently loses the fast path. Not worth a footgun that reports nothing.

Point 4 is the one to re-measure rather than trust. A 9-byte table stays in the data cache inside a tight benchmark loop, so the packed constant may not beat it there; it is chosen for the real scan cycle, where the data cache is busy with actual object payloads and the literal rides the instruction path instead. If the benchmark says otherwise, a 16-entry table indexed by the raw 4-bit type — with invalid entries marked `0xFF` so one load covers both the shift and the validity check — is the alternative worth trying.
