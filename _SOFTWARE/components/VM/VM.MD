# Virtual Machine (VM)

IMPORTANT: If you modify the source files in this component (`core/*.h`, `core/*.c`), you must update this documentation to reflect the changes.

## Overview

`VM` is the runtime for runIT's block-based scripting language. A program is compiled off-device into a flat description, uploaded wirelessly, and rebuilt here into a graph of **objects** (the data) wired to **blocks** (the behaviour), executed cyclically by a supervisor.

Three properties shape every decision in this component:

* **No dynamic allocation after load.** All storage comes from a bump allocator over one static pool. Individual objects are never freed; replacing a program resets the whole arena and rebuilds. This removes lifetime tracking, fragmentation and the allocation-failure path from the runtime entirely.
* **Structure is fixed at load; only values change at runtime.** Nothing — not a remote write, not a block — can alter an object's type, size, flags or name after it is created. Reshaping the graph means uploading a new program.
* **Everything from the wire is untrusted.** Ids, counts, offsets and lengths arriving in an upload packet are validated before they reach storage, and the whole load path returns `err_h` chains rather than sentinels.

### Directory Map
```
components/VM/
├── CMakeLists.txt
├── VM.MD                 # This documentation
└── core/
    ├── sys_error_vm.h     # VM owner map + error tag map + _OWNED error macros
    ├── vm_obj.h           # vm_obj_head_t / vm_obj_t / vm_obj_h, type widths, header helpers
    ├── vm_alloc.h/.c      # vm_alloc_t bump allocator
    ├── vm_obj_build.h/.c  # vm_obj_create()/vm_accessor_create(), object + accessor tables
    ├── vm_obj_access.h/.c # accessors, payload resolution, typed get/set macros
    ├── vm_loader.h/.c     # program storage pool, load state machine, wire-facing validation
    ├── vm_block.h         # vm_block_data_t, pin access, EN/ENO, BLOCK_CALL
    └── vm_code.h          # DRAFT — section/node/block layout, not yet implemented
```

The upload decoder table (`dec_vm_loader.h`, class `0x04`) lives in `components/codecs/decoders/` — see [[CODECS.MD]].

---

## Object Model

An object is a fixed 4-byte header followed by a flexible payload:

```c
typedef struct __packed vm_obj_head_t {
  uint16_t payload_size;      // value length in bytes
  struct { uint8_t obj_t : 4; uint8_t name_size : 4; } d;
  struct {
    uint8_t mutable : 1;      // anything may write it
    uint8_t usr_mutable : 1;  // user logic / remote writes may write it
    uint8_t upd : 1;          // value changed since last read by telemetry
    uint8_t upd_resetable : 1;
    uint8_t tagged : 1;       // name present
    uint8_t retentive : 1;    // persist to NVS
  } f;
} vm_obj_head_t;              // _Static_assert'd at 4 bytes

typedef struct __packed vm_obj_t { vm_obj_head_t head; uint8_t payload[]; } vm_obj_t;
typedef vm_obj_t* vm_obj_h;   // opaque handle, same convention as err_h
```

**The name lives at the tail**, at `payload[payload_size]` onward — not as a prefix. This is why `vm_obj_value()`/`vm_obj_payload_size()` need no branch on `tagged`: the value always starts at `payload[0]` and always runs `payload_size` bytes, whether the object is named or not. Only cold paths (`vm_obj_tag()`, NVS keying, telemetry decode) ever touch the name. `name_size` living in the header rather than as a length byte in the tail means the tag needs no length prefix at all.

Total footprint is `sizeof(head) + payload_size + name_size` (`vm_obj_total_size()`), which is also the stride an arena walker uses.

### Types and widths

```c
VM_OBJ_NONE, VM_OBJ_PTR, VM_OBJ_U8, VM_OBJ_U32, VM_OBJ_I32,
VM_OBJ_F, VM_OBJ_B, VM_OBJ_STR, VM_OBJ_U64
```

Two parallel tables in `vm_obj.h`: `vm_obj_type_sizes[]` (bytes) and `vm_obj_type_shifts[]` (log2 of the same). **Every width is a power of two**, so "how many items fit in `payload_size` bytes" is a shift, not a divide — the single hottest operation in accessor resolution, and one the compiler cannot strength-reduce itself because the width comes from a runtime lookup. Adding a non-power-of-two type would silently truncate; it needs a matching shift entry.

`VM_OBJ_STR` is **an array of chars**, not a distinct string abstraction: one byte per element, no terminator implied by the type. A whole-object accessor gives the buffer, an indexed one gives a single character, and everything that works on a `VM_OBJ_U8` array works here. It stays a separate type from `VM_OBJ_U8` because the type is what tells a client how to render the bytes — which is also why `vm_obj_copy_content()` refuses `STR` → `U8` despite the identical width.

**A new type needs an arm in every value-path switch**, not just the two tables: `vm_payload_read()`, `VM_VAL_CAST_TO()`, `store_scalar()` and `payload_as_index()`. A missing case is not a compile error — it falls to `default` and reads or writes **0**, which is indistinguishable from a genuinely empty value. `VM_OBJ_STR` shipped that way until stage J caught it.

### Nested objects

There is no composite type. A parent is a `VM_OBJ_PTR` array whose elements are `vm_obj_h` values pointing at children — the same shape Python lists, Java jagged arrays and cJSON use. Children are independently typed, tagged, flagged and addressable.

Consequences worth knowing:
* **Pointer payloads are never portable.** They are addresses in this boot's arena. `retentive` is rejected outright for `VM_OBJ_PTR` at creation, `vm_obj_copy_content()` refuses PTR on either side, and the upload protocol carries **child ids**, never addresses.
* Building is order-dependent: children first, then the parent, then link.

---

## Arena (`vm_alloc.h`)

A bump allocator over a caller-supplied buffer. `vm_alloc()` / `vm_alloc4()` hand out aligned slices; `vm_alloc_reset()` reclaims everything at once. There is no per-object free. Exhaustion emits `ERR_VM_ALLOC_EXHAUSTED` with requested-vs-remaining and returns NULL.

**Alignment note:** a payload begins at `header + 4`, so a `VM_OBJ_U64` element is only 4-byte aligned. Casting to `uint64_t*` would assert 8-byte alignment to the compiler, which on Xtensa can fault or read the wrong words — so 64-bit reads and writes go through `memcpy`, which compiles to the same two word accesses without the claim.

---

## Accessors (`vm_obj_access.h`)

### Object table

`g_vm_obj_table` maps id → `vm_obj_h` for the one loaded program. `vm_obj_by_id()` is bounds-checked and returns NULL past the end, so an empty table (between teardown and load) fails closed everywhere.

### Addressing

```c
typedef struct { vm_obj_t_e type; void* ptr; uint16_t count; } vm_payload_t;

typedef enum { VM_IDX_LITERAL, VM_IDX_REF, VM_IDX_NAME } vm_index_kind_e;

struct vm_accessor_t {
  uint16_t id;                // root object
  uint8_t count;              // chained indices; 0 = whole object
  const vm_index_t* indices;
};
```

An accessor is a root id plus a chain of index steps. Each step is:

| kind | meaning | use |
| :--- | :--- | :--- |
| `VM_IDX_LITERAL` | fixed position | compiled wiring |
| `VM_IDX_REF` | position read live from another accessor | selector wires, sequencer step counters |
| `VM_IDX_NAME` | the child whose tag matches | data whose field *order* isn't known at compile time |

`VM_IDX_NAME` requires a `VM_OBJ_PTR` parent with tagged children, and costs a scan with a compare per child — right for the handful of fields in a message, wrong for a compiled path where the position is already known. A miss raises `ERR_VM_ACCESSOR_NAME_NOT_FOUND` carrying the name, which is a routine condition for variable-shape data rather than necessarily a bug.

**A chain step landing on a `VM_OBJ_PTR` element yields the pointer slot, not the object behind it.** Reading a scalar field of a nested object therefore takes *two* steps:

```c
// msg.temp  ->  ["temp"] descends into the child, [0] reads its value
vm_index_t idx[] = {{.kind = VM_IDX_NAME, .name = "temp"},
                    {.kind = VM_IDX_LITERAL, .value = 0}};
```

Stopping after `["temp"]` leaves the payload typed `VM_OBJ_PTR`, which `vm_payload_read()` has no case for — so `VM_OBJ_GET_VAL` yields **0**, silently. This is deliberate and not auto-dereferenced, because `vm_obj_link()` needs the raw slot; a trailing auto-deref would break linking and the switch/demux pattern with it.

`vm_get_obj()` is the exception that *does* follow a trailing `VM_OBJ_PTR` element — but only when there is a chain. A count-0 accessor names the object itself, so a pointer container comes back as the container; following its first pointer would hand a relay or encoder block `child[0]` instead of the object it was wired to, with nothing to indicate the substitution.

`VM_ACCESSOR_MAX_DEPTH` (8) caps by-ref recursion — an uploaded tree is not trusted to be acyclic.

### Accessor table and sharing

Accessors have their own id space (`g_vm_accessor_table`, `vm_accessor_by_id()`), built the same way objects are. **Sharing is the expected case**: ten blocks reading the same object reference one accessor rather than carrying ten copies. Deduplication is the *compiler's* job — it holds the whole graph and knows which pins are identical, so the device just stores what it is told and pays nothing at load or at runtime. An accessor is a pure immutable descriptor with no cache, so sharing is safe by construction.

Blocks hold resolved `vm_accessor_t*`, not ids. Ids are the wire form, pointers the runtime form; resolving once at load removes the invalid-id failure mode from every subsequent access, matching outputs (`vm_obj_h`) and `VM_OBJ_PTR` children. The cost is an ordering rule — accessors must exist before whatever references them.

An accessor and its index array are one allocation: identical lifetimes, always walked together. A `VM_IDX_NAME` index additionally owns a NUL-terminated copy of its name in the arena, because the wire form is unterminated and the frame buffer does not outlive the load.

**A `VM_IDX_REF` target must already exist when the referencing index is written.** That makes reference cycles impossible to *construct*, which is a stronger guarantee than `VM_ACCESSOR_MAX_DEPTH` catching them at resolve time.

**No resolution cache.** Once item counts became a shift, an unchained resolve costs about what checking a cache stamp would. Caching only pays on multi-level PTR chains, and spending ~24 bytes per accessor across every pin to speed up the rare shape is the wrong trade. If chain-heavy programs ever show up in profiling, the invalidation hook is `vm_obj_link()`.

### Reading and writing

Conversion is automatic in both directions: the caller's declared C type drives it, so the same block body works whatever the wired source stores.

```c
float angle = 0.0f;
BLOCK_CALL(VM_OBJ_GET_VAL(angle, in0), block);   // any stored type -> float
```

Float→integer conversion **rounds and saturates** (`vm_f_to_i`). A plain cast of an out-of-range float to an integer is undefined behaviour in C and is reachable from ordinary user wiring, not just corruption. Range is *not* clamped to the destination's own limits — narrowing 300 into a `uint8_t` still wraps, so clamp in a type wide enough to see the real value **before** narrowing.

| API | Purpose |
| :--- | :--- |
| `vm_obj_get_payload(&p, acc)` | resolve to where a value lives |
| `vm_get_obj(&h, acc)` | resolve to the object itself; follows a trailing PTR, but a count-0 accessor yields the container |
| `VM_OBJ_GET_VAL(out, acc)` | one converted scalar into a local |
| `VM_OBJ_SET_VAL(in, acc)` | converted write via accessor (checks `mutable`) |
| `VM_OBJ_SET_VAL_AT(in, obj, idx)` | converted write to an owned object, no resolve |
| `VM_PAYLOAD_GET_VAL(out, payload)` | read from an already-resolved payload |
| `vm_payload_at(p, i)` | element `i` as its own payload — the array-iteration primitive |
| `vm_obj_copy_content(src, dst)` | bulk copy; same type and count, PTR rejected |
| `vm_obj_link(join, owner)` / `vm_obj_link_direct(cell, idx, child)` | point a PTR slot at an object |

Every successful write sets the target's `upd` flag and honours `mutable`.

### Writing a block — what you have to remember

Nothing, in the normal path. A block body reads and writes pins exactly as before:

```c
float a = 0;
BLOCK_CALL(VM_OBJ_GET_VAL(a, in0), block);
BLOCK_CALL(VM_OBJ_SET_VAL_AT(a * 2.0f, out0, 0), block);
```

The resolution cache lives inside the accessor and is built by the loader, so an uploaded program gets it with no cooperation from the block. The one rule is for code that builds accessors **directly in C** — bring-up fixtures, tests, benchmarks — which must call `vm_accessor_cache_build(acc)` once the root object exists and the indices are set. Forgetting is a speed loss, never a wrong answer.

Two consequences worth knowing:

* A `static const vm_accessor_t` initialiser cannot be cached — it lives in flash and there is no build call. Correct, just slower. Fine for tests; use the build API for anything on a scan path.
* `vm_obj_table_set()` is a load-time API. Re-pointing an id *after* its accessors were cached would leave them addressing the old object — rebuild the caches, or do what the loader does and rebuild the program.

**`VM_VAL_OF` and `VM_TYPE_OF` are a pair.** `VM_TYPE_OF` names the `vm_obj_t_e` that the reader (`VM_VAL_CAST_TO`) will use to pick a `vm_val_t` member, so `VM_VAL_OF` must write *that* member — `uint8_t`, `char` and `bool` write `.u8` because they are typed `VM_OBJ_U8`/`VM_OBJ_B`. Letting them fall to the `.u32` default reads back correctly on a little-endian target and silently wrong anywhere else; the arms are explicit so the pairing does not depend on byte order. Adding a type to one macro means adding it to the other.

**`resolve_fast()`** short-circuits the three accessor shapes that cover nearly every wire — `obj(id)`, `obj(id, [k])`, `obj(id, [j][k])`, literal indices only — as a few bounds checks instead of the chain loop. It returns `false` rather than an error for both "shape I don't handle" and "this failed"; the caller then falls through to `resolve_d()`, which re-walks and builds the real `err_h` with chain position and object in it. Failure is the cold path, so it is cheaper to pay for the walk twice there than to carry error construction through the hot one. Both paths must agree on what they accept — in particular the `mutable` check belongs on the deepest object in each.

---

## Blocks (`vm_block.h`)

One block is a single arena blob: `[cfg][in_cnt × const vm_accessor_t*][out_cnt × vm_obj_h][custom_data]`.

**Inputs and outputs are deliberately different shapes.** An input can be wired to anything — any object, any element, possibly through a switch cell, possibly with a dynamic index — so it needs a full accessor. An output is bound once at load to an object the block owns and never moves, so the slot holds the handle directly: smaller, and free at runtime.

### EN / ENO

Modelled on IEC 61131-3, and kept outside the numbered pins so the supervisor can gate every block identically without knowing which pin index a given block type uses. Both are ordinary boolean objects; to gate on several conditions, wire them through an Expression block and feed its single result to `EN` rather than expecting a subscription list.

```c
IF_BLOCK_ENABLED(block) {
  ... normal work ...
  vm_block_set_ENO(block, true);
} else {
  servo_go_home(...);           // defined safe state, not "whatever was last"
  vm_block_set_ENO(block, false);
}
```

The supervisor calls **every** block every cycle; the block decides what being disabled means for it. If the supervisor simply skipped disabled blocks, an actuator could never define a safe fallback, and "hold whatever was last commanded" is usually the wrong failure mode. An unwired `EN` reads as enabled; a *failed* `EN` resolve reads as disabled, because running the body when the gate cannot be evaluated is the more dangerous guess.

`BLOCK_CALL(call, block)` is the reporting boundary: block `execute()` bodies are `void`, so there is nobody above them to return an `err_h` to. It wraps a failure with this block's identity and pushes it — the role `SE_ORIGIN_CALL` plays for top-level entry points.

---

## Program Loading (`vm_loader.h`) and Wire Protocol (class `0x04`)

`vm_loader` owns a static pool (`VM_LOADER_POOL_SIZE`, default 8192 bytes), the load state, and all wire-facing validation. The decoder does framing only.

```
0x40 reset    : -
0x41 open     : u16 obj_cnt, u16 acc_cnt, u32 total_size
0x42 add objs : u8 n, n x { u16 id, u16 item_count, u8 type, u8 flags, u8 name_len, char name[name_len] }
0x43 set data : u8 n, n x { u16 id, u16 start_idx, u16 byte_len, u8 data[byte_len] }
0x44 add acc  : u8 n, n x { u16 acc_id, u16 root_obj_id, u8 idx_count, u8 idx_len, u8 idx_data[idx_len] }
                idx_data = idx_count x { u8 kind, LITERAL u32 | REF u16 acc_id | NAME u8 len + bytes }
```

All multi-byte fields little-endian. Design points:

* **`item_count`, not a byte size** — a client never needs its own copy of the type-width table. `0` is rejected (`ERR_VM_OBJ_EMPTY`): an object with no storage still has an address, and that address is the next allocation's first byte.
* **`start_idx` is unconditional.** Making its presence depend on comparing `byte_len` against the element width would leave framing dependent on a lookup the parser must trust, and would make a single non-zero element unaddressable (one element is never "larger than one element").
* **Flags travel as an explicit `VM_LOAD_F_*` byte**, not the raw header bitfield — bitfield layout is compiler-defined and a Python client cannot reproduce it reliably. `tagged` is derived from `name_len`; `upd` always starts clear.
* **For a `VM_OBJ_PTR` object, `0x43` data is a list of u16 child ids**, resolved through the table and linked. Children must therefore be created by an earlier `0x42`.
* `open` caps the arena at the **declared** `total_size` rather than the pool, so a program that under-declares then overruns fails on the object that overruns instead of silently borrowing space.

`vm_loader_reset()` detaches the table *before* resetting the arena, so for the whole teardown→load window every id resolves to NULL and accessors and late callbacks fail closed rather than into the previous program's storage. An abandoned or failed upload lands in the same safe state for free.

The class is registered in `sys_interface_init()` — see [[SYS_INTERFACE.MD]].

---

## On-Target Self Test

`components/runit/vm_selftest.c` exercises the stack bottom-up so a failure points at one layer rather than at "the VM". Enable with `RUNIT_ENABLE_VM_SELFTEST` in `runit_board_cfg.h`; results are PASS/FAIL lines on serial with a final tally.

| Stage | Covers |
| :--- | :--- |
| A | header helpers and the type tables — every width a power of two, the shift table agreeing with the size table for all types, `total_size` with and without a name, `payload_at` stepping and its bounds |
| B | value conversion — widening, U64 round-trip at a 4-byte-aligned payload, float rounding into integers, `vm_f_to_i` NaN/saturation guards, narrow source types (`bool`/`char`/`uint8_t`) landing in the union member their tag is read back through, and the deliberate *absence* of destination clamping |
| C | accessor resolution — literal/by-ref/by-name indices, live by-ref following its selector, whole-array iteration, read-vs-write mutability, and the failure modes (OOB, unknown id, unlinked slot, chain past a scalar, depth cap) |
| D | nested objects — three-level chains, mixed literal/name steps, jagged 2D, `get_obj` at each depth, aliasing one child under two parents, re-linking as the switch/demux primitive, failures at depth |
| E | mutation — direct writes, `copy_content` including its type/count/mutability/PTR rejections, `link` and `link_direct` |
| F | block API — pin arrays, `get_in`/`get_out`, EN/ENO semantics including an unresolvable EN reading as disabled, custom-data placement |
| G | object construction guards — every `vm_obj_create()` rejection (bad type, zero items, oversize payload, long name, retentive PTR, NULL args), flag round-trip, 15-char tag placement at the payload tail, zeroing over dirty arena bytes, arena exhaustion |
| H | name matching and accessor construction — prefix/length/empty/over-long queries against real tags, NULL and untagged slots in the scan, accessors built through the construction API, index-setter and table-binding guards, fail-closed behaviour after a table reset |
| I | access edge cases — `get_obj` on a whole PTR container, scalar writes into PTR refused, out-of-range and negative by-ref indices, element-level `copy_content`, the depth cap from *both* sides, the `_Generic` arms stage B does not reach |
| J | string objects — per-character read and write, `char` through the `_Generic` path, buffer iteration, bulk copy and its length/type rejections |
| K | the upload protocol as **complete injected frames** through `sys_interface_decode()` — class dispatch, the decoder's cursor walk, the loader, then read-back through the accessor layer including a by-name lookup and accessor sharing |
| L | malformed frames — truncation, `name_len`/`byte_len` past the frame end, duplicate and out-of-range ids, writes past an object, unknown child id, unknown index kind, oversize program, unknown packet byte |

Stages A–J drive the API directly; K and L inject real frames rather than calling `vm_loader_*`, so they cover the parts a direct API test would miss entirely: class registration, packet dispatch, and the cursor bounds checks. Expected-failure cases inspect the returned `err_h` without pushing it, so a clean run produces no error telemetry.

Several assertions exist to pin a decision rather than to check an obvious behaviour, and are worth leaving in place: that a tag prefix does **not** match (stage H), that a by-ref index of 65536 is out of range rather than truncated to element 0 (stage I), that a chain exactly at `VM_ACCESSOR_MAX_DEPTH` still resolves (stage I, so the cap cannot silently tighten), and that a rejected write leaves the target untouched (stages C and I).

`vm_selftest_inject(frame, len)` is exposed for ad-hoc probing — hand it any raw frame (class byte included) and it returns the decoder's `err_h` without pushing it.

Pair it with `RUNIT_SKIP_DEVICE_INIT` to boot with no I2C devices installed, which is useful when the VM is what you want to test and a missing or faulty peripheral would otherwise stall boot.

---

## Access Benchmark

`components/runit/vm_bench.c` times the access layer against the jagged 2D shape — an 8×8 float grid (`VM_OBJ_PTR[8]` of `VM_OBJ_F[8]`), read cell by cell. Enable with `RUNIT_ENABLE_VM_BENCH`; keep `RUNIT_ENABLE_VM_SELFTEST` **off** while benchmarking, since both drive `g_vm_obj_table` and the test leaves it detached.

| Scenario | Shape | What it adds over the row above |
| :--- | :--- | :--- |
| raw C pointer walk | — | the floor: the data walk with no VM |
| `flat[i]` | 1 literal index | one resolve: id bounds check, header read, offset shift |
| `grid[r][c]` | 2 literal indices | one `VM_OBJ_PTR` dereference level |
| `grid[rsel][csel]` | 2 by-ref indices | two nested resolves per access |
| `grid["rN"][c]` | name + literal | a tag scan of the parent's children |
| row resolved once, payload walk | `vm_get_obj` + `vm_payload_at` | **removes** 56 of the 64 resolves |
| `grid[r][c]` write | 2 literal indices | the `mutable` check and the `upd` flag |

Each scenario performs exactly 64 accesses per pass and returns their sum, checked against 2016 before timing — a scenario that silently stops resolving would otherwise look extremely fast. Timing is `esp_cpu_get_cycle_count()` over 500 passes, best of 3 trials, with the CPU frequency measured rather than assumed. Interrupts stay live (BLE, timers), which is what "best of 3" is for: a trial that catches an ISR is discarded rather than averaged in.

The rows are ordered so each one isolates a single added cost. The last two are the ones to design against: **the payload walk is the shape a fold block should use** (resolve the row once, step its elements), and by-name access is the expensive one — right for a parsed message, wrong for a compiled path where the position is already known.

One assertion is worth knowing about: stage K asserts that `msg["temp"]` **without** a trailing `[0]` yields 0 rather than the value, pinning the documented two-step rule so a future auto-dereference change cannot silently pass.

### What the hot path is allowed to cost

The first benchmark run put `flat[i]` at 111 cyc against a 5.2 cyc raw walk. Disassembly, not guesswork, said where it went — and none of it was the walk itself:

* **A call.** `VM_OBJ_GET_VAL` invoked an out-of-line `vm_obj_get_payload`, so every pin read paid `entry`/`retw` register-window rotation and returned its result by writing 12 bytes of `vm_payload_t` through a pointer that the caller read straight back.
* **Two dispatches.** `vm_payload_read()` switched on the stored type into a `vm_val_t`, spilled it to the stack, and `VM_VAL_CAST_TO()` switched on the same type again to load it back.
* **Two flash data loads.** `vm_obj_type_sizes[]` for the width and `vm_obj_type_shifts[]` for the count — `.rodata` lives in flash, so both went through the data cache on every element access.
* **A `mull`.** The count came from a shift, then the address came from a multiply, to arrive at a number the shift already had.

The fixes, in the order they matter:

1. **`vm_resolve_fast()` moved into the header** (`__always_inline`). The three literal shapes resolve as straight-line code with the type and pointer live in registers. No call, no window rotation, no struct round-trip.
2. **`VM_LOAD_CAST_TO()` fuses load and convert** into one dispatch, replacing read-then-cast. `vm_payload_read()` remains for callers that hold a value rather than an address.
3. **`vm_obj_elem_ptr()` works in byte offsets.** `off = i << shift` is both the address and — compared against `payload_size` — the bounds check. No count, no width lookup, no multiply.
4. **`VM_OBJ_SHIFT_PACK`** replaces the shift table with a literal. Two bits per type in one immediate, and every valid width is `1 << shift`, so the size table leaves the hot path too.

A float read of `flat[i]` now ends in a single `lsi f0, a11, 0`, and the binary got *smaller* despite the inlining, because a fused switch is less code than two.

That took `flat[i]` 111 → 66 cyc and `grid[r][c]` 141 → 105. It also made every *other* row slightly worse — by-ref 500 → 512, by-name 665 → 686, write 152 → 157 — because a shape the inline resolver does not handle now pays for the attempt before walking. That is the intended trade, but it exposed where the remaining cost actually was:

5. **A by-ref index no longer recurses when it does not have to.** `resolve_d()` called itself for every `VM_IDX_REF` step — a function with a 160-byte frame, re-entered twice per `grid[rsel][csel]` access, to read one `uint8_t` selector. It now tries `vm_resolve_fast()` on the sub-accessor first; only a selector that is itself chained or by-ref falls through to the recursion.

   **The depth cap has to be tested before that, not after.** `VM_ACCESSOR_MAX_DEPTH` is enforced by counting `resolve_d()` frames, and the fast path resolves a level without one — so letting it take the *last* level of a chain made `MAX_DEPTH + 1` fit inside `MAX_DEPTH` frames and pass. Stage C caught it: "one level past MAX_DEPTH is refused" failed while "exactly at MAX_DEPTH still resolves" passed, which is the signature of swallowing precisely one level. The fix declines the fast path at the limit and hands that level to `resolve_d()`, which refuses it and produces the same error trace as before. Worth remembering if any other shortcut is ever added below a bounded walk: a cap counted in frames breaks the moment something resolves without one.
6. **`VM_IDX_NAME` carries its length.** `find_child_by_name()` ran `strlen()` on the same constant string on every access. `vm_index_t` had padding after `kind`, so `name_len` costs nothing in size and is measured once at build. Use `VM_IDX_BY_NAME("temp")` for static initialisers — a hand-written `{.kind = VM_IDX_NAME, .name = "temp"}` leaves the length zero and matches **nothing**.
7. **Writes are inline too.** `vm_store_inline()` moved to the header, so `VM_OBJ_SET_VAL` and `VM_OBJ_SET_VAL_AT` resolve and store without a call — the latter matters most, since it is how every block publishes a result *and* what `vm_block_set_ENO()` runs for every block on every cycle. Only the error arms (`vm_obj_not_scalar_err`, `vm_obj_not_mutable_err`, `vm_obj_oob_err`, `vm_obj_null_obj_err`) stayed in the `.c`, where there is an ambient `OWNER` to build them with.
8. **`vm_get_obj()` uses the fast path** — it went straight to the walk, which cost the fold-block shape a full resolve per row.

9. **Read conversion split back out of line.** Fusing load-and-convert was right for speed and wrong for size: six arms — including a float→int64 saturation path and a U64→float conversion that drags in `__floatundisf` — measured ~450 B at *every* pin. One 2-input block came to 1.8 kB, and a 50-type palette to ~90 kB. Dispatching on the **destination** instead means only three shapes exist (float, ≤32-bit integer, 64-bit integer), so `vm_read_as_f32` / `_i32` / `_i64` — 370 B total, one copy — serve every pin in the program. `vm_resolve_fast()` stays inline, which is where the speed came from.

| symbol | before | after |
| :--- | ---: | ---: |
| one read site (`bench_flat_literal`) | 424 B | **252 B** |
| `vm_block_is_enabled` | 518 B | **300 B** |
| a 2-in/1-out block type | 1803 B | **1395 B** |
| one write site (`bench_grid_write`) | 543 B | 543 B |

Writes were deliberately left inlined. They are fewer than reads, they were the slower path, and the ~400 B they cost per site buys back the call — the right trade while flash is the plentiful resource and RAM is not.

**Array iteration inverts the trade, and one round of measurement proved it.** `VM_PAYLOAD_GET_VAL` was routed through the shared converters along with everything else, and the payload walk went 21.7 → 39.1 cyc/element — 80% slower. A pin read runs its conversion once per block per cycle, so trading ~10 cyc for ~200 B across hundreds of sites is clearly right. A fold block runs it *per element*, with the type loop-invariant, across a handful of sites. So `VM_OBJ_GET_VAL` calls (`VM_LOAD_CAST_TO`) and `VM_PAYLOAD_GET_VAL` inlines (`VM_LOAD_CAST_TO_FAST`) — reach for the second only inside a loop over elements.

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

---

## Program RAM Budget

`VM_LOADER_POOL_SIZE` is a static `.bss` array reserved whether or not a program is loaded, drawn from the same DIRAM the heap uses. It holds **everything a program owns** — objects, accessors, blocks, both tables.

| item | bytes |
| :--- | :--- |
| block | 20 (`vm_block_data_t`) + 4/input + 4/output + own variables |
| object | 4 (head) + payload, 4-byte aligned, + tag bytes if named |
| accessor | 20 + 8 per chain index — shared between pins reading the same thing, so unshared is the worst case |
| tables | 4 per object id + 4 per accessor id |

Worked from the target workload — 100 blocks, 5 pins each, one 8-byte output apiece, 16 B of own variables, **no** accessor sharing:

| | | bytes |
| :--- | :--- | ---: |
| blocks | 100 × (20 + 20 + 4 + 16) | 6.0 k |
| outputs | 100 × (4 + 8) | 1.2 k |
| constants | 100 × (4 + 8) | 1.2 k |
| accessors | 500 × (20 + 8) | 14.0 k |
| tables | 200 + 500 ids | 2.8 k |
| | **total** | **25.2 k** |

The pool is **48 kB** — about 2.5× that, covering deeper chains, larger payloads and blocks that keep more state. Raising it from the old 8 kB default took DIRAM from 147 kB to 188 kB used, leaving **153 kB** for heap, BLE and task stacks.

A program that does not fit fails its load with `ERR_VM_LOAD_TOO_BIG` carrying requested-vs-available, so the pool is never silently the reason something misbehaves — and resizing it is a deliberate act with a number attached.

Note the app **partition** is 1 MB (`CONFIG_PARTITION_TABLE_SINGLE_APP`), not the chip's 8 MB. Code size is bounded by that partition, currently 649 kB used of 1 MB.

---

## Design Notes & Known Limitations

* **Everything below the self test is still unproven.** `vm_selftest` covers the object, accessor and loader layers; blocks, EN/ENO and `vm_code.h` have never executed.
* **`vm_code.h` is a draft.** `vm_code_t` / `vm_code_section_t` / `vm_code_node_t` sketch the section→node→block layout, but no supervisor, executor or block implementation exists.
* **No block types are implemented.** The pin/EN/ENO/error scaffolding is in place; the palette is not.
* **Subscription and telemetry are not built.** The design is settled (supervisor iterates a list of ids, sends `[id][content]` on `upd`, client rebuilds objects from a manifest) but no code exists. Note `VM_OBJ_PTR` objects must be rejected as subscription targets for the same reason `retentive` rejects them.
* **Callback-driven sections are designed, not built.** Intended shape: the VM registers one route with `sys_callbacks`, events land on a VM-owned queue drained by the supervisor at a cycle boundary, and typed event blocks publish them. This keeps a single permanent registration, so a program reload swaps only an internal table and cannot leave a dangling callback into a freed arena.
* **NVS retention is not built.** Design: periodic batch flush plus flush-on-shutdown, keyed by **name not id** (ids shift between firmware builds), leaves only.
* `connected_in` is a `uint16_t` bitmask, capping a block at 16 wired inputs.

---

## API Summary

**Construction** — `vm_obj_create()`, `vm_obj_table_build()`, `vm_obj_table_set()`, `vm_obj_table_reset()`, `vm_accessor_create()`, `vm_accessor_set_literal()`, `vm_accessor_set_ref()`, `vm_accessor_set_name()`, `vm_accessor_table_build()`, `vm_accessor_table_set()`, `vm_accessor_table_reset()`
**Header helpers** — `vm_obj_total_size()`, `vm_obj_payload_size()`, `vm_obj_type_size()`, `vm_type_width()`, `vm_obj_items_cnt()`, `vm_obj_tag()`, `vm_obj_payload()`
**Arena** — `vm_alloc_init()`, `vm_alloc()`, `vm_alloc4()`, `vm_alloc_reset()`, `vm_alloc_used()`, `vm_alloc_remaining()`
**Access** — `vm_obj_by_id()`, `vm_accessor_by_id()`, `vm_obj_get_payload()`, `vm_get_obj()`, `vm_obj_set_scalar()`, `vm_obj_set_scalar_direct()`, `vm_obj_copy_content()`, `vm_obj_link()`, `vm_obj_link_direct()`, `vm_payload_at()`, `vm_obj_as_payload()`, `vm_payload_read()`
**Macros** — `VM_OBJ_GET_VAL()`, `VM_OBJ_SET_VAL()`, `VM_OBJ_SET_VAL_AT()`, `VM_PAYLOAD_GET_VAL()`, `VM_VAL_CAST_TO()`
**Blocks** — `vm_block_get_in()`, `vm_block_get_out()`, `vm_block_is_enabled()`, `vm_block_set_ENO()`, `vm_block_inputs()`, `vm_block_outputs()`, `vm_block_custom_data()`, `vm_block_size()`, `IF_BLOCK_ENABLED()`, `BLOCK_CALL()`
**Loading** — `vm_loader_reset()`, `vm_loader_open()`, `vm_loader_add_obj()`, `vm_loader_set_data()`, `vm_loader_add_accessor()`, `vm_loader_state()`, `vm_loader_used()`

---

## Error Ownership

Owners `0xA900`–`0xA907` (`SYS_VM_OWNER_MAP` in `core/sys_error_vm.h`): `OWNER_VM_BASE`, `OWNER_VM_ALLOC`, `OWNER_VM_OBJ`, `OWNER_VM_ACCESSOR`, `OWNER_VM_BLOCK`, `OWNER_VM_CODE`, `OWNER_VM_LOADER`, `OWNER_DEC_VM_LOADER`.

Tags (`SYS_ERROR_VM_MAP`) cover allocation (`ERR_VM_ALLOC_EXHAUSTED`), resolution (`ERR_VM_ACCESSOR_UNKNOWN_ID`, `_OOB`, `_TYPE_MISMATCH`, `_NULL_OBJ`, `_DEPTH_EXCEEDED`, `_INDEX_FAILED`, `_NAME_NOT_FOUND`, `_NOT_MUTABLE`), blocks (`ERR_VM_BLOCK_INPUT_UNRESOLVED`, `_PIN_MISSING`, `_PIN_UNLINKED`, `_FAILED`), construction (`ERR_VM_OBJ_BAD_TYPE`, `_EMPTY`, `_TOO_LARGE`, `_NAME_TOO_LONG`, `_RETENTIVE_PTR`, `_COPY_MISMATCH`, `_TABLE_OOB`, `_TABLE_DUP`) loading (`ERR_VM_LOAD_BAD_STATE`, `_TOO_BIG`, `_DATA_RANGE`, `_SHORT_RECORD`) and accessors (`ERR_VM_ACC_TABLE_OOB`, `_TABLE_DUP`, `ERR_VM_ACC_INDEX_OOB`, `ERR_VM_ACC_BAD_KIND`).

`sys_error_vm.h` also supplies `SE_*_OWNED` variants (`SE_EMIT_ERR_OWNED`, `SE_ERR_NEW_OWNED`, `SE_RET_ERR_OWNED`, `SE_WRAP_ERR_OWNED`, `SE_CHECK_NOT_NULL_OWNED`, `SE_CHECK_IF_ALLOCATED_OWNED`) that take the owner explicitly, for header-only code pulled into translation units with their own ambient `OWNER`.

Because `sys_errors` aggregates this map, it reaches `core/sys_error_vm.h` through a raw include path rather than `REQUIRES VM` — VM already requires `sys_errors`, so a component dependency back would be circular.
