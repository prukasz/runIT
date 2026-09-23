# Virtual Machine (VM)

Related docs: [VM_EXEC.MD](VM_EXEC.MD) (the execution model and why), [VM_OBJ_ACCESS.MD](core/obj/VM_OBJ_ACCESS.MD) (object/accessor API with examples), [VM_OBJ_CONTRACTS.md](core/obj/VM_OBJ_CONTRACTS.md) (lifetime, ownership, mutation rules), [VM_PERF.MD](VM_PERF.MD) (hot-path measurements and decisions — history).

IMPORTANT: If you modify the source files in this component (`core/**`, `blocks/**`), you must update this documentation to reflect the changes.

## Overview

`VM` is the runtime for runIT's block-based scripting language. A program is compiled off-device into a flat description, uploaded wirelessly, and rebuilt here into a graph of **objects** (the data) wired to **blocks** (the behaviour), executed cyclically by a supervisor.

Three properties shape every decision in this component:

* **No dynamic allocation after load** — with one deliberate exception. The pool is heap-allocated once per upload, sized by the program itself; from then until the next upload every allocation is a bump within it. Individual objects are never freed; replacing a program resets the whole arena and rebuilds. This removes lifetime tracking, fragmentation and the allocation-failure path from the runtime entirely.

  The exception is `VM_BLK_CLONE`, which builds its destination on the heap through `vm_obj_dyn`. It allocates when the source schema changes, including tag identity. A matching destination is validated and refilled in place. Replacements are built and filled before publication, then the old tree is released. Dynamic owning links are shared, acyclic, and depth-bounded.
* **Each object's schema and permission flags are fixed after load.** Values, pointer links, and freshness can change at runtime. Clone replaces objects when a message schema changes; it never resizes an existing object. Block construction protects its output objects before execution starts.
* **Everything from the wire is untrusted.** Ids, counts, offsets and lengths arriving in an upload packet are validated before they reach storage, and the whole load path returns `err_h` chains rather than sentinels.

### Directory Map
```
components/VM/
├── CMakeLists.txt
├── VM.MD, VM_EXEC.MD, VM_PERF.MD
├── errors/
│   └── sys_error_vm.h          # VM owner map + error tag map + _OWNED error macros
├── blocks/                     # THE PALETTE — block types & dispatch tables
│   ├── vm_blocks.h             # type ids (VM_BLK_*), table declarations, vm_block_fn_for()
│   ├── vm_blocks_table.c       # g_vm_block_types[]: one entry per type (body, check, shape, state size)
│   ├── vm_block_expr.h         # EXPR (float) and EXPR_BIT (uint32) — RPN bytecode
│   ├── vm_block_branch.h       # IF (two-way) and SWITCH (n-way) flow routers
│   ├── vm_block_for.h          # FOR — span owner / loop runner
│   ├── vm_block_set.h          # SET — explicit copy, source -> destination
│   ├── vm_block_clone.h        # CLONE — copy building destination dynamically
│   ├── vm_block_edge.h         # EDGE — edge detector (rising, falling, both)
│   ├── vm_block_timer.h        # TIMER — timers (TON, TOF, TP + inverted), time units
│   ├── vm_block_io_set_level.h # IO_SET_LEVEL — drive a pin (sys_io)
│   ├── vm_block_io_toggle.h    # IO_TOGGLE — toggle a pin (sys_io)
│   ├── vm_block_latch.h        # LATCH — SR / RS bistable
│   ├── vm_block_periodic.h     # PERIODIC — "every N" tick
│   ├── vm_block_action.h       # ACTION — request a system action (queued)
│   └── vm_block_on_event.h     # ON_EVENT — start a chain on a matching system event
└── core/
    ├── errors/
    │   └── vm_errors.h/.c      # block/pin error builders
    ├── store/
    │   └── vm_store.h/.c       # THE storage API — per-load pool, bump arena, three id registries
    ├── obj/
    │   ├── vm_obj.h            # vm_obj_head_t / vm_obj_t / vm_obj_h, type widths, header helpers
    │   ├── vm_obj_build.h/.c   # vm_obj_create() / vm_accessor_create()
    │   ├── vm_obj_dyn.h/.c     # heap objects, refcounted, register + teardown
    │   └── vm_obj_access*.h/.c # accessors, payload resolution, typed get/set macros, copy/clone/link
    ├── block/
    │   ├── vm_block.h          # vm_block_data_t, pin access, EN/ENO, cfg.rt, spans, BLOCK_CALL
    │   ├── vm_block_build.h/.c # vm_block_create(): wiring resolution + verify
    │   ├── vm_block_helpers.h/.c # shape checks, optional pins, gates, user-mutation boundary
    │   └── vm_block_run.c      # what a block body reaches for: ask, claim a span
    ├── exec/
    │   ├── vm_event.h/.c       # routed sys_event events — queue + per-pass snapshot
    │   ├── vm_override.h/.c    # runtime variable updates between scan loops
    │   └── vm_exec.h/.c        # the supervisor: pass loop, dispatch, spans, watchdog, control, action requests
    ├── loader/
    │   └── vm_loader.h/.c      # load state machine, wire-facing validation
    ├── retain/
    │   └── vm_retain.h/.c      # retained values: capture, save (retain task), restore by name
    └── sub/
        └── vm_sub.h/.c         # telemetry subscriptions (packet 0x47)
```

The groups are stacked bottom-up: `obj` builds on `store`, `block` on `obj`, and `exec` on `block`. The loader owns the program lifecycle. The store's teardown also calls the dynamic allocator so resetting storage always reclaims both allocation domains.

`blocks/` sits outside `core/` and depends on it, never the reverse. The supervisor dispatches through the *declarations* in `vm_blocks.h` and never learns what is in the tables — which is what keeps `core/` free of `sys_io` and every other driver a real block pulls in. Anything above the VM's layer (system actions) is reached through a function the application registers (`vm_exec_register_action_request()`), never by include.

**Every include across the project is by bare filename** — `#include "vm_obj.h"`, never `"obj/vm_obj.h"`. Each group is on the include path instead (see `CMakeLists.txt`), so this tree is a reading aid and regrouping it again later touches no source file. The one path that is spelled out is `../VM/errors` in `sys_errors/CMakeLists.txt`, which reaches the error map without a circular `REQUIRES`.

The upload decoder table (`dec_vm_loader.h`, class `0x04`) lives in `components/codecs/decoders/` — see [[CODECS.MD]].

---

## Object Model

The current lifetime, ownership, schema, write-policy, and publication contracts
are specified in [VM_OBJ_CONTRACTS.md](core/obj/VM_OBJ_CONTRACTS.md).

An object is a fixed 4-byte header followed by a flexible payload:

```c
typedef struct __attribute__((aligned(4))) vm_obj_head_t {
  uint16_t payload_size;      // value length in bytes
  struct { uint8_t obj_t : 4; uint8_t name_size : 4; } d;
  struct {
    uint8_t mutable : 1;      // anything may write it
    uint8_t upd : 1;          // published this pass
    uint8_t upd_resetable : 1;
    uint8_t tagged : 1;       // name present
    uint8_t retentive : 1;    // persist to NVS
    uint8_t dynamic : 1;      // registered heap allocation
    uint8_t usr_protected : 1;// user writes denied; internal writes still allowed
    uint8_t _pad : 1;
  } f;
} vm_obj_head_t;              // _Static_assert'd at 4 bytes

typedef struct vm_obj_t { vm_obj_head_t head; uint8_t payload[]; } vm_obj_t;
typedef vm_obj_t* vm_obj_h;   // opaque handle, same convention as err_h
```

**The name lives at the tail**, at `payload[payload_size]` onward — not as a prefix. This is why `vm_obj_get_payload_ptr()`/`vm_obj_get_payload_size()` need no branch on `tagged`: the value always starts at `payload[0]` and always runs `payload_size` bytes, whether the object is named or not. Only cold paths (`vm_obj_get_tag()`, NVS keying, telemetry decode) ever touch the name. `name_size` living in the header rather than as a length byte in the tail means the tag needs no length prefix at all.

Total footprint is `sizeof(head) + payload_size + name_size` (`vm_obj_get_total_size()`), which is also the stride an arena walker uses.

### Types and widths

```c
VM_OBJ_NONE, VM_OBJ_PTR, VM_OBJ_U8, VM_OBJ_U32, VM_OBJ_I32,
VM_OBJ_F, VM_OBJ_B, VM_OBJ_STR
```

Two parallel tables in `vm_obj.h`: `vm_obj_type_sizes[]` (bytes) and `vm_obj_type_shifts[]` (log2 of the same). **Every width is a power of two**, so "how many items fit in `payload_size` bytes" is a shift, not a divide — the single hottest operation in accessor resolution, and one the compiler cannot strength-reduce itself because the width comes from a runtime lookup. Adding a non-power-of-two type would silently truncate; it needs a matching shift entry.

`VM_OBJ_STR` is **an array of chars**, not a distinct string abstraction: one byte per element, no terminator implied by the type. A whole-object accessor gives the buffer, an indexed one gives a single character, and everything that works on a `VM_OBJ_U8` array works here. It stays a separate type from `VM_OBJ_U8` because the type is what tells a client how to render the bytes — which is also why `vm_obj_copy_content()` refuses `STR` → `U8` despite the identical width.

**A new type needs an arm in every value-path switch**, not just the two tables: `vm_internal_scalar_type()`, `VM_INTERNAL_LOAD_SCALAR()`, `vm_internal_store_inline()` and `payload_as_index()`. A missing case is not a compile error — it falls to `default` and reads or writes **0**, which is indistinguishable from a genuinely empty value. `VM_OBJ_STR` once shipped that way, reading back 0.

### Nested objects

There is no composite type. A parent is a `VM_OBJ_PTR` array whose elements are `vm_obj_h` values pointing at children — the same shape Python lists, Java jagged arrays and cJSON use. Children are independently typed, tagged, flagged and addressable.

Consequences worth knowing:
* **Pointer payloads are never portable.** They are addresses in this boot's arena. `retentive` is rejected outright for `VM_OBJ_PTR` at creation, `vm_obj_copy_content()` **walks** a PTR array rather than copying its bytes — so a copy never duplicates an address — and the upload protocol carries **child ids**, never addresses.
* Building is order-dependent: children first, then the parent, then link.

---

## Arena (inside `vm_store.c`)

`vm_alloc_t` — `base` / `capacity` / `offset` — is the bump region the pool is carved out of. Chunks are handed out sequentially at 4-byte alignment and **never freed piecemeal**: a program is sized once, built once, and replaced whole, so there is no per-object free and no need for one. The arena is re-armed at each new program's declared size rather than reset in place. Exhaustion emits `ERR_VM_ALLOC_EXHAUSTED` with requested-vs-remaining and returns NULL.

This was its own `vm_alloc.h`/`.c` pair until [vm_store.h](core/store/vm_store.h) became the only caller — a header, a source file and a public API guarding a boundary that had nothing on the other side of it. Carving is now `static` inside `vm_store.c`; the struct stays in the header only because `vm_store_t` embeds it and `vm_store_get()` is inline.

**Alignment note:** scalar payload elements are at most four bytes wide.

---

## Accessors (`vm_obj_access.h`)

### Object registry

`vm_obj_get_by_id()` maps id → `vm_obj_h` for the one loaded program, over the shared registry in [vm_store.h](core/store/vm_store.h). It is bounds-checked and returns NULL past the end, so an empty registry (between teardown and load) fails closed everywhere. Accessors and blocks have identical registries — see *One store, three registries*.

### Addressing

```c
typedef struct { vm_obj_t_e type; void* ptr; uint16_t count; } vm_payload_t;

typedef enum { VM_IDX_LITERAL, VM_IDX_REF, VM_IDX_NAME } vm_index_kind_e;

struct vm_accessor_t {
  uint16_t id;                // root object
  uint8_t count;              // chained indices; 0 = whole object
  uint8_t flags;              // VM_ACC_F_CACHED
  const vm_index_t* indices;  // allocated in the same chunk, right after the header
  vm_obj_h c_owner;           // resolution cache, filled by vm_accessor_cache_build()
  void* c_ptr;
  uint16_t c_count;
  uint8_t c_type;
  uint8_t c_pad;
};                            // 20 bytes + 8 per index
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

Stopping after `["temp"]` leaves the payload typed `VM_OBJ_PTR`, so `VM_OBJ_SCALAR_GET` returns an error and preserves its output. This is deliberate and not auto-dereferenced, because `vm_obj_link()` needs the raw slot; a trailing auto-deref would break linking and the switch/demux pattern with it.

`vm_get_obj()` is the exception that *does* follow a trailing `VM_OBJ_PTR` element — but only when there is a chain. A count-0 accessor names the object itself, so a pointer container comes back as the container; following its first pointer would hand a relay or encoder block `child[0]` instead of the object it was wired to, with nothing to indicate the substitution.

`VM_ACCESSOR_MAX_DEPTH` (8) caps by-ref recursion — an uploaded tree is not trusted to be acyclic.

### Accessor table and sharing

Accessors have their own id space (`vm_accessor_get_by_id()`), built the same way objects are. **Sharing is the expected case**: ten blocks reading the same object reference one accessor rather than carrying ten copies. Deduplication is the *compiler's* job — it holds the whole graph and knows which pins are identical, so the device just stores what it is told and pays nothing at load or at runtime.

An accessor is immutable *after load*, but it is no longer cacheless: `vm_accessor_cache_build()` writes the resolution cache once, at load, before anything reads it. Sharing stays safe because the cached resolution is a property of the accessor, not of who is reading it — every sharer would resolve to the identical address. What it does mean is that an accessor must live in writable storage, which rules out putting one in flash and is why the loader builds them all in the arena.

Blocks hold resolved `vm_accessor_t*`, not ids. Ids are the wire form, pointers the runtime form; resolving once at load removes the invalid-id failure mode from every subsequent access, matching outputs (`vm_obj_h`) and `VM_OBJ_PTR` children. The cost is an ordering rule — accessors must exist before whatever references them.

An accessor and its index array are one allocation: identical lifetimes, always walked together. A `VM_IDX_NAME` index additionally owns a NUL-terminated copy of its name in the arena, because the wire form is unterminated and the frame buffer does not outlive the load.

**A `VM_IDX_REF` target must already exist when the referencing index is written.** That makes reference cycles impossible to *construct*, which is a stronger guarantee than `VM_ACCESSOR_MAX_DEPTH` catching them at resolve time.

**Only stable addresses are cached.** Whole-object and single-literal root accessors qualify. Deeper chains follow links live and never cache a replaceable child's address. Every arena accessor, including its cache, expires with its program.

### Reading and writing

Conversion is automatic in both directions: the caller's declared C type drives it, so the same block body works whatever the wired source stores.

```c
float angle = 0.0f;
BLOCK_CALL(VM_OBJ_SCALAR_GET(angle, in0), block);   // any stored type -> float
```

Float→integer conversion **rounds and saturates** (`vm_f_to_i`). A plain cast of an out-of-range float to an integer is undefined behaviour in C and is reachable from ordinary user wiring, not just corruption. Range is *not* clamped to the destination's own limits — narrowing 300 into a `uint8_t` still wraps, so clamp in a type wide enough to see the real value **before** narrowing.

| API | Purpose |
| :--- | :--- |
| `vm_obj_get_payload(&p, acc)` | resolve to where a value lives |
| `vm_get_obj(&h, acc)` | resolve to the object itself; follows a trailing PTR, but a count-0 accessor yields the container |
| `VM_OBJ_SCALAR_GET(out, acc)` | one converted scalar into a local |
| `VM_OBJ_SET_SCALAR(in, acc)` | converted write via accessor (checks `mutable`) |
| `VM_OBJ_SET_SCALAR_AT_IDX(in, obj, idx)` | converted write to an owned object, no resolve |
| `VM_PAYLOAD_GET_VAL(out, payload)` | read from an already-resolved payload |
| `vm_payload_get_at(p, i)` | element `i` as its own payload — the array-iteration primitive |
| `vm_obj_copy_content(src, dst)` | bulk copy; same type and count at every leaf, PTR arrays walked pairwise (deep, allocation-free) |
| `vm_obj_link(join, owner)` / `vm_obj_link_direct(cell, idx, child)` | point a PTR slot at an object |

Every ordinary successful write sets the owning object's `upd` flag and honors `mutable`; user APIs also reject `usr_protected`. Copy validates the entire destination before committing. Clone additionally publishes its holder on both replacement and refill. Scalar writes do not propagate freshness to ancestors: use `vm_obj_mark_updated()` after completing an aggregate update. `vm_obj_clear_quiet()` deliberately does not publish freshness.

### Writing a block — what you have to remember

Nothing, in the normal path. A block body reads and writes pins exactly as before:

```c
float a = 0;
BLOCK_CALL(VM_OBJ_SCALAR_GET(a, in0), block);
BLOCK_CALL(VM_OBJ_SET_SCALAR_AT_IDX(a * 2.0f, out0, 0), block);
```

The resolution cache lives inside the accessor and is built by the loader, so an uploaded program gets it with no cooperation from the block. The one rule is for code that builds accessors **directly in C** — bring-up fixtures, tests, benchmarks — which must call `vm_accessor_cache_build(acc)` once the root object exists and the indices are set. Forgetting is a speed loss, never a wrong answer.

Two consequences worth knowing:

* A `static const vm_accessor_t` initialiser cannot be cached — it lives in flash and there is no build call. Correct, just slower. Fine for tests; use the build API for anything on a scan path.
* Binding is a load-time act. Re-pointing an id *after* its accessors were cached would leave them addressing the old object — and `vm_store_alloc()` refuses a rebind outright (`ERR_VM_REG_DUP`), so within one load this cannot happen at all; across loads the program is rebuilt whole.

**`VM_VAL_OF` and `VM_TYPE_OF` are a pair.** `VM_TYPE_OF` names the `vm_obj_t_e` that the reader (`VM_INTERNAL_LOAD_SCALAR`) will use to pick a `vm_val_t` member, so `VM_VAL_OF` must write *that* member — `uint8_t`, `char` and `bool` write `.u8` because they are typed `VM_OBJ_U8`/`VM_OBJ_B`. Letting them fall to the `.u32` default reads back correctly on a little-endian target and silently wrong anywhere else; the arms are explicit so the pairing does not depend on byte order. Adding a type to one macro means adding it to the other.

**`resolve_fast()`** short-circuits the three accessor shapes that cover nearly every wire — `obj(id)`, `obj(id, [k])`, `obj(id, [j][k])`, literal indices only — as a few bounds checks instead of the chain loop. It returns `false` rather than an error for both "shape I don't handle" and "this failed"; the caller then falls through to `resolve_d()`, which re-walks and builds the real `err_h` with chain position and object in it. Failure is the cold path, so it is cheaper to pay for the walk twice there than to carry error construction through the hot one. Both paths must agree on what they accept — in particular the `mutable` check belongs on the deepest object in each.

---

## Blocks (`vm_block.h`)

One block is a single arena blob: `[cfg][in_cnt × const vm_accessor_t*][out_cnt × vm_obj_h][custom_data]`.

**Inputs and outputs are deliberately different shapes.** An input can be wired to anything — any object, any element, possibly through a switch cell, possibly with a dynamic index — so it needs a full accessor. An output is bound once at load to an object the block owns and never moves, so the slot holds the handle directly: smaller, and free at runtime.

### EN / ENO

Modelled on IEC 61131-3, and kept outside the numbered pins so the supervisor can gate every block identically without knowing which pin index a given block type uses. Both are ordinary boolean objects.

Several enable sources on one block combine per `en_mode`: **any** for a merge ("either branch reached me"), **all** for independent conditions. Both are needed — two branches of one split rejoining is an OR, while "on rising edge *and* while `A > B`" is an AND, and running that as an OR would drive the actuator on every edge regardless of the condition. Anything richer than those two is still an Expression block feeding one result in.

**Reading another block's ENO.** There is no special mechanism, because ENO is an ordinary object: the *producer* holds `cfg.eno` as a `vm_obj_h`, and a *consumer* reads it through a normal accessor rooted at that object's id. The same input/output asymmetry as every other pin — a block owns its outputs as handles and reaches everything else through accessors — and the same ids-are-the-wire-form rule that governs accessors.

To gate block B on block A's ENO:

| | |
| :--- | :--- |
| `0x42` | create a `VM_OBJ_B` object, 1 item, mutable → it gets an object id |
| block A | `cfg.eno` points at that object |
| `0x44` | create a chainless accessor `object(eno_id)`, `idx_count = 0` |
| block B | that accessor is one entry in B's enable list |

Two consequences worth planning for:

* **Every ENO is addressable for free.** `vm_loader_add_obj()` binds an id unconditionally, so there is no wire path to an unbound object — an ENO created by `0x42` is in the object registry like any other, and `vm_store_alloc()` rejects a duplicate id so two blocks cannot claim the same slot. (Only hand-built C fixtures can produce an unbound object, by calling `vm_obj_create()` without binding.) A block whose ENO is never consumed can leave `cfg.eno` NULL and skip the write entirely — `vm_block_set_eno()` no-ops on NULL.
* **A chainless accessor is the cheapest shape there is** — `count == 0` is exactly what `vm_accessor_cache_build()` pre-resolves, so an EN read costs about what `flat[i]` does (~34 cyc), not what a chain costs. Gating is not a thing to be economical about.

**EN is a list, not a single accessor.** The flow graph is a DAG: a block reached by two branches has two enable sources. `cfg.en_mode` says how they combine — `VM_BLK_EN_ANY` for branches rejoining, `VM_BLK_EN_ALL` for independent conditions that must all hold. `en_cnt == 0` is a root that always runs, and below two sources the mode is irrelevant. The list lives in the block's own trailing bytes alongside the pins (`[cfg][inputs][outputs][en list][custom]`), so a root pays nothing and the common single-enable case pays one pointer — see [[VM_EXEC.MD]] for where the DAG comes from.

**Sequential Block Order.** `block_idx` records sequential execution order. `BLOCK_CALL` runs inside block bodies where there is no supervisor frame to ask — attaching block identity directly to error reports.

An EN that fails to resolve reads as **disabled**, not as enabled, so a broken gate stops a block rather than freeing it to run. That is the opposite of *absence* on purpose: no sources means nothing is gating this block, while a source that exists but cannot be read means the gate is broken. A failing source is reported and counts as false, so one broken branch cannot mask a working one that would have enabled the block.

```c
IF_BLOCK_ENABLED(block) {
  ... normal work ...
  vm_block_set_eno(block, true);
} else {
  servo_go_home(...);           // defined safe state, not "whatever was last"
  vm_block_set_eno(block, false);
}
```

The supervisor calls **every** block in topological order each scan pass and never interprets `EN` itself; the block decides what being disabled means for it. If the supervisor skipped disabled blocks, an actuator could never define a safe fallback — "hold whatever was last commanded" is usually the wrong failure mode — and a block whose *active* state is `EN` false, such as an off-delay timer or a falling-edge detector, could never run at all.

Execution granularity is the **scan cycle / pass**: every block in the registry is visited unless a span owner claims and jumps over its range. An unwired `EN` reads as enabled; a *failed* `EN` resolve reads as disabled, because running the body when the gate cannot be evaluated is the more dangerous guess.

`vm_block_is_enabled()` reports the level and nothing else. Edge detection and timing state are block-type concerns and live in that block's own `custom_data` struct — a block that wants an edge is stateful by definition and already has somewhere to keep it.

`BLOCK_CALL(call, block)` is the reporting boundary: block `execute()` bodies are `void`, so there is nobody above them to return an `err_h` to. It wraps a failure with this block's identity and pushes it — the role `SE_REPORT` plays for top-level entry points.

---

## Program Loading (`vm_loader.h`) and Wire Protocol (class `0x04`)

`vm_store` owns the pool and all allocation; `vm_loader` owns the load state and all wire-facing validation. The decoder does framing only.

A command from a client is `[seq][0x04][packet][payload]`; `sys_interface` strips `seq` and echoes it in the response (SYS_INTERFACE.MD), so everything below starts at the packet byte.

```
0x40 reset    : -
0x41 open     : u16 obj_cnt, u16 acc_cnt, u16 blk_cnt, u32 total_size
0x42 add objs : u8 n, n x { u16 id, vm_obj_head_t head (4 B), char name[head.d.name_size] }
0x43 set data : u8 n, n x { u16 id, u16 start_idx, u16 byte_len, u8 data[byte_len] }
0x44 add acc  : u8 n, n x { u16 acc_id, u16 root_obj_id, u8 idx_count, u8 idx_len, u8 idx_data[idx_len] }
                idx_data = idx_count x { u8 kind, LITERAL u32 | REF u16 acc_id | NAME u8 len + bytes }
0x45 add block: u16 blk_id, u16 block_idx, u8 type, u8 in_cnt, u8 q_cnt, u8 en_cnt, u8 en_mode,
                u8 on_error, u16 custom_len, u16 eno_obj_id,
                in_cnt x u16 acc_id (0xFFFF = unwired), q_cnt x u16 obj_id, en_cnt x u16 acc_id,
                u8 custom_data[custom_len]
0x47 subscribe: u8 n (0 = clear all), n x u16 obj_id           -- telemetry, see "Subscriptions"
0x48 exec     : u8 command (vm_exec_command_e)                  -- see "Execution control packet"
```

All multi-byte fields little-endian. Design points:

* **`payload_size` in bytes, not an element count.** This reversed once: the wire used to carry `item_count` so a client needed no type-width table. Sizing is the client's job now, so it has that table anyway, and in exchange the device does **no arithmetic on sizes at all** — which removes the overflow that multiplying a 16-bit count by a width and truncating back into 16 bits used to risk. Two rejections replace it: `0` (`ERR_VM_OBJ_EMPTY`) because an object with no storage still has an address, and that address is the next allocation's first byte; and a size that is not a whole number of elements (`ERR_VM_OBJ_BAD_SIZE`), because 5 bytes of `U32` would let `vm_obj_elem_ptr()` return an element whose last three bytes lie past the payload. A wrong-but-aligned size is *contained* rather than rejected — every access is bounds-checked against it.
* **`start_idx` is unconditional.** Making its presence depend on comparing `byte_len` against the element width would leave framing dependent on a lookup the parser must trust, and would make a single non-zero element unaddressable (one element is never "larger than one element").
* **The object header travels as its raw 4-byte in-memory image** (`VM_OBJ_HEAD_WIRE_SIZE`), bitfields included. Bitfield layout is compiler-defined, so the layout is pinned instead: `vm_obj_head_t` is published with `@wire-abi esp32-gcc-bitfield-v1` and explicit `@wire-offset`/`@wire-bit-offset` per field in `data-structures/vm/vm-model.generated.json`, and the app packs from that. `vm_obj_create()` forces `upd`, `tagged` (from `name_size`) and `dynamic` whatever the wire says.
* **For a `VM_OBJ_PTR` object, `0x43` data is a list of u16 child ids**, resolved through the table and linked. Children must therefore be created by an earlier `0x42`.
* `open` caps the arena at the **declared** `total_size` rather than the pool, so a program that under-declares then overruns fails on the object that overruns instead of silently borrowing space.

`vm_loader_reset()` first prevents new passes and waits for the current pass to return. It then clears execution state and subscriptions and resets both allocation domains. IDs resolve to NULL afterwards; raw handles and cached accessors have expired and must not be reused by callbacks.

The class is registered by runit's `runit_register_decoders` boot step (`components/runit/runit_decoders.c`) — see [[SYS_INTERFACE.MD]].

---

## Packet → Objects: the whole load path

Six packets, four components, one arena. Nothing is allocated outside it and nothing survives a reload.

```
transport provider (BLE write, ...) -> interface data connector
   │
   ▼
sys_interface RX task                          components/system/sys_interface
   │  strips seq; data[0] = class header, matched against registered classes
   ▼
dec_vm_loader_decode(data+1, len-1)            components/codecs/decoders
   │  data[0] = packet header 0x40..0x48, walks records with a bounds-checked cursor
   ▼
vm_loader_*()                                  components/VM/core/loader/vm_loader.c
   │  state machine + wire→internal translation
   ▼
vm_obj_create / vm_obj_link_direct /           components/VM/core/obj/vm_obj_build.c
vm_accessor_create / vm_accessor_cache_build
   │
   ▼
vm_store_alloc()  →  pool malloc'd at 0x41 open components/VM/core/store/vm_store.c
```

The class is bound once at boot by runit's `runit_register_decoders` boot step (`components/runit/runit_decoders.c`):
`sys_interface_register_decoder(CONFIG_RX_PACKET_CLASS_VM_LOADER, dec_vm_loader_decode, "vm_loader")`.

### The loader packets

| | packet | payload | effect |
| :--- | :--- | :--- | :--- |
| **0x40** | reset | — | `vm_loader_reset()` |
| **0x41** | open | `u16 obj_cnt, u16 acc_cnt, u16 blk_cnt, u32 total_size` | reset, cap arena, build all three id tables |
| **0x42** | add objects | `u8 n`, then `{u16 id, vm_obj_head_t head, char name[]}` | create + bind each object |
| **0x43** | set data | `u8 n`, then `{u16 id, u16 start_idx, u16 byte_len, u8 data[]}` | fill a payload, **or link children** |
| **0x44** | add accessors | `u8 n`, then `{u16 acc_id, u16 root_obj_id, u8 idx_count, u8 idx_len, u8 idx_data[]}` | create + pre-resolve each accessor |
| **0x45** | add block | one block: header, in ids, out ids, en ids, private state | check the type is registered, create + resolve all wiring |

0x42, 0x43 and 0x44 are batched and repeatable — a program arrives across as many frames as the MTU needs. **0x45 carries exactly one block**, because a block record varies in length four separate ways (inputs, outputs, enable sources, private state); batching would need a length prefix per record and a cursor that can desynchronise, and one-per-frame puts a malformed record's blast radius at that block.

### One store, three registries

Objects, accessors and blocks are three id spaces over **one** arena, and they go through one API — [vm_store.h](core/store/vm_store.h):

```c
vm_store_open(total, counts)     // size the arena, build the registries
vm_store_alloc(&p, r, id, n)     // carve, zero, bind -- in one step
vm_store_get(r, id)              // id -> pointer, NULL past the end
vm_store_reset()                 // detach every registry, then reset the arena
```

They had been three copies of the same code: three table structs, three `build`/`set`/`reset` triples, three pairs of out-of-range/duplicate error tags, differing only in the pointer type. Now `vm_obj_get_by_id()`, `vm_accessor_get_by_id()` and `vm_block_get_by_id()` are one-line typed casts over `vm_store_get()`, and the three construction functions are the only things that know what a chunk means. Six error tags collapsed into `ERR_VM_REG_OOB` / `ERR_VM_REG_DUP` carrying a `kind`, so a trace still reads *"accessor registry: id 7 is already bound"*.

`vm_store_alloc()` also folds three steps that were previously separate and individually forgettable — allocate, zero, bind — into one. Zeroing is not optional: the arena hands back whatever the last program left there, and every consumer reads as "empty" only if those bytes are clear. The id is validated **before** any arena is spent, so a program with a bad id leaves the space its retry needs.

`VM_ID_NONE` allocates without binding, for storage reached through something else — an accessor's copied name bytes — or built by a block at runtime.

**All three allocate the same shape: header, then trailing bytes, in a single chunk.** An object is `[head][payload][tag]`, a block is `[cfg][ins][outs][custom]`, an accessor is `[header][indices]`.

The accessor is the one that expresses it differently: `indices` is a *pointer* into its own tail rather than a flexible array member. A FAM was tried and reverted — it saves 4 bytes and one dependent load per chain step, but C forbids a struct containing a flexible array member from being a member of another struct (C11 6.7.2.1p3), which is exactly what a compile-time `static const` accessor needs. The alternatives were a GCC extension or a hand-duplicated layout no assertion could protect. Since the resolution cache already removes that load from every shape that runs hot, the trade came down to 4 bytes against losing compile-time accessors entirely.

### Step by step

**0x40 — reset.** `vm_loader_reset()` owns the complete lifecycle: stop pass admission, wait for active execution, reset execution and subscriptions, then reset dynamic objects, three registries, and the arena. It returns with execution stopped. Transport callbacks remain installed, but no program handle survives.

**0x41 — open.** Replaces whatever was loaded, so an upload that restarts mid-stream cannot mix two programs — but **allocates the new pool first**: `total_size` is checked against `CONFIG_VM_STORE_MAX_POOL` and the largest free internal-DRAM block (`ERR_VM_LOAD_TOO_BIG`, carrying requested-vs-available), and only once the new pool is secured is the old program torn down. A failed open leaves the running program, its subscriptions and its run mode intact. The arena is exactly the **declared** size — a program that under-declares fails on the object that overruns, which points at the bug, instead of quietly borrowing space it never asked for. The three id tables are bump-allocated and zeroed. State becomes `VM_LOAD_OPEN`.

**0x42 — objects.** The loader passes the received `vm_obj_head_t` directly to `vm_obj_create()`, which takes that head plus a name pointer — there is no separate config type. The name is copied straight out of the frame, because `name_size` in the head says how many bytes to take, so the wire form needs no NUL and no staging buffer.

`payload_size` arrives already in bytes, so this layer does no arithmetic on sizes — nothing to overflow, nothing to truncate. The type is checked before use (`ERR_VM_OBJ_BAD_TYPE`). `vm_obj_create()` then checks what it still can — type, zero payload, the retentive-PTR rule — and hands the size to `vm_store_alloc()`, which carves 4-aligned, **zeroes** it (the arena still holds the previous program's bytes) and binds the id, rejecting duplicates and out-of-range ids before spending any arena. The head is copied in, `upd`/`tagged`/`dynamic` are forced to the creator's values whatever the caller asked for, and the tag is appended.

Passing a wrong `payload_size` is therefore possible, and contained: `vm_obj_elem_ptr()` bounds every access against it, so an undersized object refuses reads past its end rather than reaching into its neighbour.

**0x43 — data, and the tree.** Two paths, chosen by the object's own type:

* **`VM_OBJ_PTR`** — the bytes are a list of `u16` **child ids**, never addresses: a pointer is meaningless outside this boot's arena. Each child must already exist, so a forward reference is a hard error rather than a dangling slot. This is where the object *tree* forms.
* **anything else** — `byte_len` must be a whole number of elements and `start_idx + n` must fit, then a straight `memcpy` into the payload.

**0x45 — blocks, and why they are last.** A block names everything else: inputs and EN are accessor ids, outputs and ENO are object ids. Every one must already be bound, which is the same ordering rule a `VM_OBJ_PTR` parent has with its children, one level up. All of them are **resolved to pointers here, once** — the block stores no ids at all, so nothing downstream can hold one that stopped being valid, and no execution path ever pays a table lookup.

The id is checked against the block table *before* anything is built, and every reference is resolved *before* anything is allocated. The check against the block's palette entry (`vm_block_verify()`: pin counts, required pins, `custom_data` size, then the type's own check of enum fields) needs the built block, so it runs after allocation — and a block it rejects is taken back out with `vm_store_undo()`: unbound, its arena space returned, its outputs' protection untouched. Either way a rejected block costs the arena nothing and never runs, so the retry still has the space it needs. `VM_BLOCK_NO_ID` (0xFFFF) now spells "absent" for **ENO only** — the block publishes nothing. An absent EN has no such spelling because it does not need one: it is simply `en_cnt == 0`, and a *listed* enable that fails to resolve is a malformed program, exactly like an unwired numbered pin.

Three shape limits, all enforced at load — `in_cnt`, `q_cnt` and `en_cnt` each ≤ 16 — for one reason: the decoder stages every id list on its stack while resolving, and a `uint8_t` count would make that a 510-byte buffer inside a BLE callback. Sixteen is well past any real block shape.

**0x44 — accessors.** Index records are decoded by kind: `LITERAL` takes a `u32`; `NAME` copies its bytes into the arena NUL-terminated and stores `name_len` so the scan never calls `strlen`; `REF` names an accessor id that **must already exist** — which is what makes a reference cycle unconstructable, rather than merely depth-capped later at resolve time. The id is bound by `vm_store_alloc()` as part of the same chunk that holds the index array, and `vm_accessor_cache_build()` then pre-resolves the accessor if its shape qualifies.

### Ordering rules

| requirement | if violated |
| :--- | :--- |
| `0x41` before everything | `ERR_VM_LOAD_BAD_STATE` |
| a child object before the `0x43` that links it | `ERR_VM_ACCESSOR_UNKNOWN_ID` |
| a referenced accessor before the one referencing it | `ERR_VM_REG_OOB` (`kind` = accessor registry) |
| every object and accessor a block names, before the block | `ERR_VM_BLK_BAD_REF`, naming the slot |
| a block's type registered in the palette, before the block | `ERR_VM_BLK_UNKNOWN_TYPE` |
| a block that passes its type's check (shape, required pins, private state) | `ERR_VM_BLK_BAD_SHAPE`; the block is removed again |
| `0x42`/`0x43`(PTR links)/`0x44`/`0x45` only while stopped | `ERR_VM_LOAD_BAD_STATE` (a running `0x43` is a runtime override instead) |
| a root object before the accessor rooted on it | **no error** — the accessor stays uncached, costing speed only |

### On failure

Every stage returns an `err_h` chain up through the decoder to `sys_interface`. The loader stays `OPEN` with the arena partially consumed — nothing is rolled back, because nothing needs to be: an id that was never set still reads NULL, so a half-built program is never *incorrectly* resolvable, only incomplete. The next `0x40` or `0x41` clears it.

### Program Ready State

A loaded program has its objects, accessors, and blocks resolved in bump storage with pin wirings bound and checked. `vm_exec_pass()` runs the topological block order (`vm_exec_run_range(0, count)`), evaluates activations and span jumps, honours `cfg.on_error`, dispatches through `g_vm_blocks[]`, and sweeps `upd` at cycle end. `vm_exec_start()` runs this loop as a dedicated task on Core 1.

---

## Execution (`core/exec/`)

The model this implements is [[VM_EXEC.MD]] — read that for *why*. This is what exists in code.

### The Pass Loop

```
scan entry:   latch clock (vm_now_ms) and routed events (vm_event_drain)
block scan:   walk blocks 0..blk_cnt; span owners claim and run nested ranges
drain point:  apply pending runtime overrides (vm_override_drain)
end of pass:  sample telemetry subscriptions (vm_sub) -> sweep upd flags
```

`vm_exec_pass()` runs one pass on the calling task; `vm_exec_start()` puts that loop on a task pinned to **Core 1**, yielding one tick per pass. At `CONFIG_FREERTOS_HZ = 100` that one tick is the 10 ms floor and feeds Core 1's idle task watchdog.

Runtime `0x43` writes received while execution is active are queued by
`vm_override_post()` and applied at the next pass boundary. This path accepts
only scalar and non-pointer array payloads. `VM_OBJ_PTR` payloads contain native
object handles, so a runtime override returns
`ERR_VM_OVERRIDE_PTR_UNSUPPORTED` instead of copying wire bytes into pointer
slots. Pointer topology is constructed while stopped through
`vm_loader_set_data()`/`vm_obj_link_direct()`, where wire child IDs are resolved
and ownership checks run. The queue drain revalidates the record length, target
type, write policy, and range before every copy; a record that no longer matches
the target is reported and discarded without publishing `upd`.

Pause and mode changes take effect before the next block dispatch, including nested span bodies. Time and events are latched at scan entry.

Stopping has separate asynchronous and synchronous entry points.
`vm_exec_request_stop()` is safe from any task, block, or sample hook: it marks
the active pass cancelled, which prevents the next block dispatch. While that
pass unwinds, `vm_exec_status().stop_requested` is true and the prior mode
remains visible. `VM_RUN_STOPPED` is published only after the pass has released
its block/accessor/object handles. `vm_exec_stop()` performs the same request
and waits until `scan_active` and `waiting` are both false. Stop preserves the
loaded program, object values, and block private state; reset remains the
operation that unloads them.

`vm_exec_pass()` is also the explicit synchronous/manual execution API. A
non-supervisor task may call it for one pass while the run mode is
`VM_RUN_STOPPED`, as bring-up fixtures do. A pending stop
request disables that exception, and the supervisor always remains parked in
`VM_RUN_STOPPED`.

A task cannot synchronously wait for the pass it is currently executing.
Calling `vm_exec_stop()` there still requests cancellation but returns
`ERR_VM_EXEC_SELF_BARRIER`. Program mutation uses the exclusive
`vm_exec_program_lock()`/`vm_exec_program_unlock()` barrier and rejects the same
self-wait without changing storage. Reset, open, and every stopped-loader
object/data/accessor/block mutation take this barrier, so a visible STOPPED
mode cannot race an active pass or a concurrent loader mutation.

### Execution control packet

All controls use one packet type: `04 48 command` (three bytes total).
`packet_vm_exec_t` carries the command byte; additional payload is rejected.

| Command | Name | Behavior |
| :--- | :--- | :--- |
| `00` | Scan mode | Hold before the next block; wait for `01`. |
| `01` | Execute once | Complete the current scan, or start one; then hold. Requires scan mode. |
| `02` | Block mode | Hold before the next block; wait for `03`. |
| `03` | Execute next | Admit one block dispatch; then hold. Requires block mode. |
| `04` | Reset to start | Cancel the current scan and hold at its beginning. |
| `05` | Normal mode | Resume continuous scan execution. |
| `06` | Pause | Hold before the next dispatch, preserving mode and pending step. |
| `07` | Resume | Restore the mode/step saved by pause. A completed scan step stays held. |
| `08` | Reset | Unload the program and clear executor/events/subscriptions; leave stopped. |
| `09` | Acknowledge fault | Clear a latched critical device fault while quiescent and stopped. Does not restart. |
| `0A` | Forget retained values | Erase the stored retained values; saving stops until the next start. |

A FOR entry consumes one step, and each dispatch inside its body consumes
another, including repeated visits in later iterations. Returning from a span
consumes no extra step. Disabled blocks still consume their dispatch step.
Missing registry entries are skipped. The final dispatch completes the scan,
samples subscriptions and clears `upd`; an empty scan completes on one step.

Reset to start retains object values, private block state, queued events and
completed-scan statistics. It clears resettable `upd` flags from the cancelled
scan and cannot undo physical outputs or restore upload-time values. In normal
mode it leaves the executor paused; resume runs again. In scan/block mode it
waits for once/next. Span implementations must check `vm_exec_cancelled()`
after returning from nested execution, as FOR does, before further writes.

Commands do not queue an unlimited number of steps: a duplicate pending step,
a NEXT while a dispatch is still running, a command for the wrong mode, or a
control during program replacement returns `ERR_VM_EXEC_CONTROL`. Receipt
means acceptance, not execution completion. `vm_exec_status()` is a locked
C snapshot of mode, scan activity and the next block while parked; existing
subscription telemetry still samples only completed scans.

A critical system-device fault latches its first device id and root owner/tag,
counts repeats, cancels the active pass, and blocks supervisor and explicit
manual passes. Program reset preserves that latch. Restart controls return
`ERR_VM_EXEC_FAULT_LATCHED` until command `09` acknowledges it; the client must
then send the desired normal, scan, or block-mode command separately. The
system response waits for the stop barrier before freezing devices and running
the configured critical action. Warning and notice device errors only run their
configured action.

The supervisor starts stopped after boot tests/benchmarks and before BLE RX
is bound, so remote commands execute on the VM task on core 1.

### Activation is the block's, not the supervisor's

**Every block in the program sequence is called.** The supervisor tests nothing,
gates nothing and skips nothing.

That is where the knowledge is. A block knows what wakes it — an arrival, a
level, a deadline, nothing at all — better than any table describing it from
outside, and it already holds everything needed to answer. So it asks:

| helper | question |
| :--- | :--- |
| `vm_block_triggered()` | did anything new arrive on any of my inputs? |
| `vm_block_input_fresh(pin)` | was it *that* one? |
| `vm_block_is_enabled()` | do my enable sources say so? |
| `vm_block_set_eno(b, false)` | not acting — and this is the *entire* else-branch |

and the ordinary block body is:

```c
static void blk_expression(vm_block_h b) {
  IF_BLOCK_TRIGGERED(b) {
    ...
    vm_block_set_eno(b, true);
  } else {
    vm_block_set_eno(b, false);         // nothing arrived, so nothing changed
  }
}
```

**A block that does not act keeps its outputs and drops its ENO. Nothing else.**
Nothing changed, so last pass's answer is still the right one — an expression
whose inputs are all old computes the same result, and zeroing it would publish
a 0 where a correct number was standing. Data persists; what is withdrawn is
the flow.

Nothing else is needed because **the supervisor already withdraws freshness for
everybody**: the end-of-pass `upd` sweep means an output nobody rewrote reads
as stale on the next pass for free. So both kinds of downstream block stand
down on their own — one gated on the ENO level, one waiting on an arrival that
never came — and the flow below prunes itself by data.

There is no "clear my outputs" call, and there was one. It existed for outputs
carrying *flow* rather than data — a router's `YES`/`NO` — on the theory that a
router which fired once would otherwise leave `YES` true forever. That only
bites if something reads `YES` as a **level**, and an event router's consumers
are update-driven: they gate on freshness, which the sweep already withdraws.
The call was solving a problem the sweep had already solved.

The write is **asymmetric, and `vm_block_set_eno()` owns the asymmetry** so no
block body can get it wrong: **true is loud** (an assertion of flow is news, so
it sets `upd`), **false is quiet** (a loud false would read as an *arrival* to
an update-driven block below — the exact opposite of standing down).

An actuator's `when inactive: Hold` then needs no mechanism at all — the block
is called like every other and simply never sets ENO false, holding its output
and its flow alike.

`vm_block_triggered()` latches `VM_BLK_RT_TRIGGERED` in `cfg.rt`, so the answer
survives the call for telemetry and debugging. Blocks that never ask pay
nothing for the feature existing.

### The palette

One `const` array in `blocks/vm_blocks_table.c`, indexed by `block_type`, one
entry per block type. That is all of it — there is no palette module, no
registry, no registration call and no indirection:

```c
typedef struct vm_block_type_t {
  vm_block_fn run;           // body, called every pass
  vm_block_verify_fn check;  // extra private-state check at load (enum ranges), or NULL
  uint8_t min_in, min_q;     // pins the block needs
  uint16_t required_in;      // bit n: input n must be wired
  uint16_t state_len;        // custom_len it needs at least
} vm_block_type_t;

const vm_block_type_t g_vm_block_types[] = {
    /* [VM_BLK_NONE] stays empty: an unset block_type must not be runnable. */
    [VM_BLK_EXPR] = VM_BLOCK_TYPE_EXPR,
    ...
};
```

Each entry is defined by its block's own header as `VM_BLOCK_TYPE_<NAME>`, next
to the body, the state struct and the catalog annotations that describe it, so
everything about a block type lives in one file. The table is the id → entry
map; adding a block type is its header plus one line here (recipe:
`.claude/skills/runit-esp/vm/blocks.md`).

**Shape is checked once, at load, from data.** `vm_block_verify()` (run by
`vm_block_create()`) checks a built block against its entry — pin counts,
required pins, `custom_len` — then calls the type's `check` for what data can't
say (an enum field in range, at least one of two pins wired). A rejected block
is removed again (`vm_store_undo()`), so a malformed program never reaches the
scan and bodies never re-check their own shape on every pass.

**The same entry feeds the app.** `generate-vm-blocks.py` reads each header's
`//#vm-block` annotations and its `VM_BLOCK_TYPE_<NAME>` macro and writes
`data-structures/vm/vm-blocks.generated.json`: id, title, category, pins (with
required flags from `required_in`), and the private-state layout with byte
offsets, enum references and device-owned (`@runtime`) fields — cross-checked
against each struct's `_Static_assert`. What the app compiles against is what
the device checks.

C lets a later designated initialiser silently override an earlier one, so the
component builds with **`-Woverride-init`** to make a doubly-claimed type a
warning rather than a last-one-wins surprise.

A firmware has exactly one palette, so the array *is* what dispatch indexes —
nothing can point it somewhere else. It lives beside the blocks rather than in
`core/exec/` on purpose: the supervisor dispatches through the declarations in
`vm_blocks.h` and nothing more, so the dependency runs `blocks/` → `exec/` and
never back. That is what keeps `core/` free of every driver a real block pulls in.

**Type 0 is reserved.** An unset `block_type` is zero, and it must not resolve
to something runnable. `vm_exec_check_block_type()` refuses a type with no entry
at load, before anything is allocated.

### What ships today

Fifteen blocks (`blocks/vm_blocks.h`). Pins marked ★ are required (verify
rejects the block without them); every other input falls back to a constant in
`custom_data` when unwired. ENO is optional on every block.

| Id | Type | Header | Inputs | Outputs | `custom_len` | Acts when |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | `EXPR` | `vm_block_expr.h` | any, named by the code | ≥1: result (float) | 4 + 4·consts + code | an input is fresh **and** enabled |
| 2 | `EXPR_BIT` | `vm_block_expr.h` | 〃 | ≥1: result (u32) | 〃 | 〃 |
| 3 | `IF` | `vm_block_branch.h` | ★ condition | ≥2: yes, no (flow) | 0 | enabled |
| 4 | `SWITCH` | `vm_block_branch.h` | ★ selector | 1..16 branches (flow) | 0 | enabled |
| 5 | `FOR` | `vm_block_for.h` | start, end, step | iterator | 24 | enabled; claims its span always |
| 6 | `SET` | `vm_block_set.h` | ★ src, ★ dst | — | 0 | src fresh **and** enabled |
| 7 | `CLONE` | `vm_block_clone.h` | ★ src, ★ dst pointer cell | — | 0 | src fresh **and** enabled |
| 8 | `EDGE` | `vm_block_edge.h` | ★ signal, threshold | pulse | 12 | enabled |
| 9 | `TIMER` | `vm_block_timer.h` | ★ IN, PT | Q, ET | 32 | enabled |
| 10 | `IO_SET_LEVEL` | `vm_block_io_set_level.h` | ★ level, pin | — | 16 | enabled |
| 11 | `IO_TOGGLE` | `vm_block_io_toggle.h` | pin | — | 16 | rising edge of enable |
| 12 | `LATCH` | `vm_block_latch.h` | set, reset (≥1 wired) | Q | 4 | enabled |
| 13 | `PERIODIC` | `vm_block_periodic.h` | period | tick | 16 | enabled |
| 14 | `ACTION` | `vm_block_action.h` | action id | — | 4 | rising edge of enable |
| 15 | `ON_EVENT` | `vm_block_on_event.h` | — | value, count | 4 | enabled **and** a matching event this pass |

Each header opens with the block's pin diagram, behaviour and `custom_data`
byte layout; that comment is the reference for the layout. The sections below
cover the design of each.

### Expression blocks

One machine with two words — `EXPR` evaluates in `float`, `EXPR_BIT` in
`uint32_t` — over an RPN instruction stream the client compiles and ships in
the block's own `custom_data`:

```
u8 const_cnt | u8 rt | u16 code_len | u32 consts[const_cnt] | u8 code[code_len]
```

The literals precede the code so they land 4-aligned with no pad: `custom_data`
always starts 4-aligned and the header is exactly 4 bytes, so reading one is an
aligned load. `rt` is the block's own scratch byte and must be zero on the wire.

They are two block types rather than one with a mode byte because the palettes
barely overlap — there is no sensible `ROL` on a float, no `SQRT` on a bit
pattern — and a mode byte would make every program carry the answer to a
question `block_type` already asks. Only the first six opcodes (`END`, `IN`,
`K`, `DUP`, `DROP`, `SWAP`) are numbered alike, so an encoder or disassembler
can handle pushes and stack plumbing without knowing which block it is aimed at.

**Straight-line, by construction.** There is no jump, so an expression cannot
loop and its worst-case duration is `code_len` — which is what lets one sit
inside a scan cycle whose duration has to stay computable. A branch is `SEL`, which
evaluates both arms; a loop is `FOR` and a span.

**Activation is `IF_BLOCK_TRIGGERED(b) IF_BLOCK_ENABLED(b)`: an input must be
fresh and the enable list must allow it.** An expression is a function of its
inputs, so re-evaluating one nothing arrived on has a known answer; the enable
list additionally lets a program gate it in place (an expression with no enable
sources is always enabled, so the common case is freshness alone). Pins are read at most once each per evaluation and only
if the code names them, so every occurrence of `IN 2` is the same number even
through a dynamic accessor, and a declared-but-unused pin costs nothing.

**Two fault classes, two lifetimes, because this runs at 100 Hz.** A malformed
program (`ERR_VM_EXPR_BAD_CODE` — bad opcode, truncated operand, stack under- or
overflow, a stack not ending on exactly one value) is a standing condition, so
it is reported once and latched in `VM_BLK_RT_CFG_BAD`, exactly as a malformed
span is. A bad *value* (`ERR_VM_EXPR_MATH` — zero divisor, negative square root,
a result that came out non-finite) belongs to this pass's data: it is reported
on entering a fault episode and re-armed by the next clean evaluation, so a
divisor stuck at zero for a minute is one error rather than six thousand. Either
way nothing is published — the output stands and `cfg.on_error` decides the flow.

`VM_BLK_RT_CFG_BAD` (0x40) is generic, not expression-specific: any block type
can latch "my private state is malformed" in it and report once per load.

### Flow routers

`IF` takes one condition and drives two boolean outputs; `SWITCH` takes one
selector and drives up to sixteen. Both carry **no `custom_data` at all** —
`in_cnt` names the selector pin, `q_cnt` names the branch count, and there is
nothing else to say — which also makes them the only stateless blocks in the
palette: the one-hot is recomputed from the input every pass and nothing is
carried between them.

**Their outputs are flow, not data, and that inverts the standard rule.** The
standard rule for ordinary data blocks is that a block which does not act withdraws only
the *flow* and leaves its outputs standing, because an output holds data and
last pass's data is still the right answer. A router has no data — its outputs
are read as downstream **enable sources**, so they follow the flow's rule: driven
on every pass the block is enabled, cleared on every pass it is not. One that
stood down leaving branch 3 high would hold the whole subtree below it enabled
through a closed gate, and because branch outputs feed enable lists that leak
propagates down the DAG rather than stopping at one block.

For the same reason **a router retracts its own outputs on a failure**, which no
other block should do. `cfg.on_error` is the supervisor's to apply and all it can
reach is ENO — the right and sufficient net for a block whose outputs are data,
and no net at all for one whose outputs gate.

Clearing is *quiet*, exactly as `vm_block_set_eno(b, false)` is: a loud false
reads as an arrival to an update-driven block below, the opposite of standing
down. The taken branch is loud. That asymmetry is the whole of what makes a
branch output usable as an enable source.

**Selection.** `IF` reads the pin as a float and takes output 0 when it is
non-zero — truthiness is `!= 0.0f`, the same test the expression palette's
`AND`/`OR`/`XOR`/`NOT` use, so a condition computed in `EXPR` and one tested here
always agree. `SWITCH` reads the pin as an `int32`, so a float source **rounds**
(`vm_get_as_i32`, as every float-to-integer pin read in the VM does) rather than
truncating — a router converting its own way would be the odd pin out. Case
values are dense: branch index *is* the selector value, because remapping a
sparse set onto `0..n-1` is one `EXPR` block and a value→branch table would cost
every switch in every program 64 bytes it does not use.

A selector outside `0..q_cnt-1` takes **no branch** — every output clears, ENO
goes false — and is *not* reported. It is an ordinary runtime state, and at scan
rate reporting it would bury the log; a program wanting a default branch drives
one from an `EXPR` range test. The two genuine faults (too few outputs for the
type, an unwired selector pin) are standing conditions and latch in
`VM_BLK_RT_CFG_BAD` to report once per load, like malformed bytecode.

### Spans

A block that repeats or skips part of the order keeps the range in its own
private state, like any other parameter, and **claims** it at run time with
`vm_block_claim_span()`. That sets `VM_BLK_RT_SPAN`; the walker reads the bit
*after* the call and jumps, because only the block knows it is a span owner.

The contract that matters: **claim first, unconditionally**, before deciding
anything else. A FOR that returns early without claiming — disabled, or count
zero — leaves the walk about to run the span inline, which is the one outcome
neither branch wanted. Running a span zero times is not the same as letting
somebody else run it once.

Nesting is capped at `VM_EXEC_MAX_SPAN_DEPTH`. A claim that doesn't move the
walk forward is reported once (sticky `VM_BLK_RT_SPAN_BAD`) and then ignored,
so it degrades to running inline rather than to a hang.

**A block's fault is its own.** `cfg.on_error` acts on `VM_BLK_RT_FAULT`, a
per-call bit in the block's own `cfg.rt` (set by `vm_block_report_error()`,
`BLOCK_CALL` or `vm_block_mark_failed()`, read with `vm_block_failed()`, cleared
by `run_block()` before every call). A span owner is *mid-call* while the blocks
in its body run through the same `run_block()`, and each of them marks only
itself — so a FOR never loses its ENO because something in its body failed, and
a FOR that failed can't have its fault cleared by a body block. An earlier
version kept one global flag and had to save and restore it around every nested
call to get the same result.

The **block watchdog** is deliberately not nested. During a span the word names
whichever body block is currently running, which is the granularity that matters
— a hung body block is named correctly. A span owner's own duration is not
watched because it is bounded by construction: `count × span`, both static.

### FOR

The span owner, and the only block that runs other blocks. It is a C `for` loop
over a float iterator:

```
for (i = start; i <cmp> end; i = i <op> step)
```

`start`, `end` and `step` each come from an input pin *or*, when that pin is
unwired, from a constant in `custom_data` — the standard unwired-pin rule, so a
loop with nothing wired is fully static and a loop with `end` wired walks however
many elements the array actually holds. `cmp` (`<`, `<=`, `>`, `>=`) and `op`
(`+`, `-`, `*`, `/`) are always the block's own, because they are the *shape* of
the loop rather than data flowing through it. There is deliberately no `!=`: on a
float it is the one condition that misses its own exit.

`custom_data` is `[vm_span_t span][f32 k_start][f32 k_end][f32 k_step][u16
max_turns][u8 op][u8 cmp][u8 rt][pad]`, 24 bytes. The span is first because that
is not this block's choice: `vm_block_span()` reads one off the head of *any*
block's `custom_data` and `vm_block_claim_span()` writes one back through the
same bytes, so the walk and the block read one copy of the range rather than two
that can disagree.

**`max_turns` is a budget, not the count, and it is what makes a span
schedulable.** The loop runs its real condition, so the turn count is whatever
the pins work out to — which means a program can phrase a loop that never ends. A
`step` of zero. `MUL` by one. A step pointing away from `end`. This runs on core 1,
where the block watchdog cannot help: it names whichever
*body* block is currently running, and those keep changing, so a spinning FOR
looks like healthy progress right up until the task watchdog resets the board.

So `max_turns` is checked every turn and ends the loop when reached. Worst-case
loop duration stays `max_turns × span` whatever the pins say, and every
non-terminating loop becomes a bounded one that reports itself
(`ERR_VM_FOR_BAD_LOOP`, latched in `VM_FOR_RT_BAD`, re-armed by a loop that ends
on its own condition) instead of hanging. Hitting the budget always means the
loop did not finish by itself, so it is always worth reporting. The same tag
covers an iterator that goes non-finite — an overflow, or `DIV` by a zero step.

**The iterator accumulates**, `i = i <op> step` applied to the running value
exactly as C does it, rather than recomputed as `start + n·step`. That is the
semantics the name promises and the only definition that means anything for `MUL`
and `DIV`. The cost is C's cost: a fractional step drifts, so `0.0` stepping by
`0.1` toward `1.0` may turn nine times or eleven. Integer starts and steps are
exact to 2²⁴, far past any loop that could finish inside a scan cycle.

**A malformed FOR still claims.** If `custom_len` holds a span but not a count,
the block claims the range and then runs it zero times, latching
`VM_BLK_RT_CFG_BAD`. Not claiming would leave the body about to execute once,
uncontrolled, in the middle of the walk — and for a body that drives actuators,
once is strictly worse than never.

**The iterator is what makes a span worth having.** If the block declares an
output, `i` is published to output 0 before each turn, *loudly*, so an
update-driven block in the body re-triggers every turn rather than only on the
first. That object is what a by-ref accessor in the body roots its index at,
which is the whole mechanism for walking an array — wire a `U32` object to it and
the pin conversion rounds the float for you. A FOR with no outputs is a plain
repeat, which is a real thing to want.
Accumulating needs no mechanism either: `upd` is swept at the end of the **pass**,
not per turn, so a block inside the span reading an object it also writes sees
its own last value and re-triggers — a sum is one `EXPR` wired back to itself,
plus something ahead of the loop to zero the accumulator.

**ENO goes true before the body**, not after it. A block
inside the span gated on the FOR's ENO has to see it on turn 0; asserting
afterwards would gate the first turn on last pass's answer, and on the first pass
on nothing at all. The flow is genuinely asserted for as long as the body runs.

### SET

`VM_BLK_SET` copies the source payload into the target. Update-driven on IN0 (`src`) and stateless (`custom_len = 0`).

This is the explicit Copy the execution model asks for on any path from an event output to a consumer that sits behind a time gate: an event object holds the *latest* payload rather than the one that activated a given block, so a block resuming in a later pass has to take its own snapshot while the triggering pass is current.

Both pins are inputs (`IN0` = source, `IN1` = destination), so Set produces no data output. `ENO` is the only output and reports one thing: the copy happened this pass.

Copies whole payloads, not scalars, and the whole tree under them: `vm_obj_copy_content()` walks pointers rather than copying raw references, so the two trees keep their own children and edits through the copy never reach the original. It will not convert — types and element counts must agree at every leaf. The destination must pre-exist and match the source's shape; nothing is allocated.

### CLONE

`VM_BLK_CLONE` is like Set, except it builds the destination instead of requiring one. Update-driven on IN0 (`src`) and stateless (`custom_len = 0`).

Where a Set copies into an object the program already declared, a Clone owns what it writes into: `IN1` names a *pointer cell*, and the block keeps a tree of its own hanging off it. This is essential for wire-deserialized objects whose shape is not known at build time.

Allocation happens only on a schema change (including tag identity). The first pass builds the tree on the heap (`vm_obj_dyn`); every subsequent pass with an identical schema finds a matching destination and copies values into it without allocation. The replacement is filled before publication, and the previous tree is released via its slot reference count.

Dynamic objects are heap-backed and isolated: nothing else in a running program allocates after load.

### EDGE

`VM_BLK_EDGE` detects transitions on `in[0]` (boolean, float, integer types).

- **Modes**: `VM_EDGE_RISING` (0 -> 1, or increase >= threshold), `VM_EDGE_FALLING` (1 -> 0, or decrease >= threshold), `VM_EDGE_BOTH` (any toggle, or |delta| >= threshold).
- **Threshold / Hysteresis**: Optional dynamic threshold on `in[1]`. If unwired, falls back to `change_by` in `custom_data`.
- **Outputs**: Optional `out[0]` pulse (driven to 1 for exactly 1 pass, then cleared); `ENO` flow pulse (true for 1 pass, then false).
- **Private State**: 12 bytes (`vm_block_edge_data_t`): `edge_type` (1B), `flags` (1B), `change_by` (4B), `prev_val` (4B). Latching `VM_EDGE_F_INITIALIZED` prevents false pulses on the very first pass.

### TIMER

`VM_BLK_TIMER` implements the IEC 61131-3 timers over the pass-latched clock (`vm_now_ms()`).

- **Modes**: `VM_TIMER_TON` (on-delay), `VM_TIMER_TOF` (off-delay), `VM_TIMER_TP` (pulse), plus inverted `TON_INV`, `TOF_INV`, `TP_INV`. `VM_TIMER_F_INVERTED` in `flags` inverts too; the two combine by XOR.
- **Time base**: `time_base` selects the unit of `PT` and `ET` — ms, s, min or h (`vm_timer_unit_e`). Internally time is kept in ms (`u64 start_ms`).
- **Inputs**: ★`in[0]` IN; `in[1]` PT (falls back to `custom_data.pt` when unwired).
- **Outputs**: `out[0]` Q (bool), `out[1]` ET (elapsed, in `time_base` units, `u32`); ENO follows Q as a level.
- **Disabled**: the timer resets, Q and ET clear quietly, ENO drops.
- **Private state**: 32 bytes, 8-aligned: `u8 mode`, `u8 flags`, `u8 time_base`, pad, `u32 pt` @8, `u64 start_ms` @16, `u32 elapsed` @24 (offsets are `_Static_assert`ed). Runtime fields are zero on the wire.

### IO_SET_LEVEL and IO_TOGGLE

The hardware blocks drive a pin on any IO device through `sys_io` (`SYS_IO_REF(device_id, pin)`), so the same block works for an ESP GPIO or an expander pin.

- **Pin**: a constant `default_io_num`, or a dynamic pin from an input. A dynamic pin must be in `allowed_mask` (64-bit). The mask is the program's own declaration of which pins it may select, not a protection boundary — protecting board pins is `sys_io`'s job (locked pins, onboard devices).
- **IO_SET_LEVEL** (16 B state): ★`in[0]` level. Writes on change only (cached last level and pin) unless `VM_IO_SET_F_ALWAYS`, sparing the I2C bus. `disabled_action` decides what disabled means: HOLD, force LOW or force HIGH.
- **IO_TOGGLE** (16 B state): toggles once per rising edge of its enable.
- ENO pulses when the hardware write succeeded this pass.

### LATCH

`VM_BLK_LATCH` is the IEC SR / RS bistable, and the answer to the model's sharpest edge: an event (or EDGE, or PERIODIC) enables for one pass, so a time gate below it resets the next pass and never completes. A latch turns the one-pass pulse into a held level: event → latch (set) → wait → act → latch (reset).

- `mode`: set dominant (`Q = SET or (Q and not RESET)`) or reset dominant (`Q = not RESET and (SET or Q)`).
- SET and RESET are levels; one pass is enough. Either may be unwired (reads false), not both (verify).
- Q is held in the block's state (4 bytes), so it outlasts the pulse. The Q output is written only when it changes, loudly, so update-driven blocks below see the transition. ENO follows Q as a level, like TIMER.
- Disabled: Q is held, ENO drops quietly.

### PERIODIC

`VM_BLK_PERIODIC` is "Every N" from [[VM_EXEC.MD]]: a one-pass tick (ENO and the optional TICK output, loud) every period while enabled — the root of a timed chain.

- First tick on the pass the block becomes enabled, then every period. Disabling stops it; re-enabling restarts the phase.
- The deadline advances by exactly the period, so the rate doesn't drift, and a late pass ticks at once.
- **Time stretching, not bursting**: never more than one tick per pass. Ticks a slow pass missed entirely are dropped and reported once per overrun episode (`ERR_VM_PERIODIC_OVERRUN`, carrying the count), re-armed by the next on-time tick. The report does not mark the block failed, so `on_error` can't withdraw a valid tick. The pass (10 ms floor) is the resolution.
- The period comes from `in[0]` or the constant, in `time_base` units (`vm_timer_unit_e`, as TIMER). A constant must be non-zero (verify); a zero from the pin pauses ticking.
- Private state: 16 bytes, 8-aligned: `u32 period`, `u8 time_base`, `u8 flags`, pad, `u64 next_ms` (runtime).

### ACTION

`VM_BLK_ACTION` requests a system action ([[SYS_ACTIONS.MD]]): static (freeze, safe state, device reset, …) or a recorded packet sequence. This is the whole VM↔actions binding [[VM_EXEC.MD]] describes.

- Fires once per rising edge of its enable: once per pulse from EDGE, PERIODIC or an event; a block with no enable sources fires once, when the program starts.
- **It never runs the action itself.** An action may stop, rewind or unload the VM (static actions 5 and 6), which a pass can't do to itself, and a recorded replay outlasts the block watchdog. The block calls `vm_exec_request_action()`, which hands the request to the function the application registered (`vm_exec_register_action_request()`; runit registers `sys_actions_request()`, a queue served by the actions task). The VM never includes `sys_actions`.
- ENO pulses on the pass the request was queued. The action's own result is reported by the actions task, after this pass; a full queue is `ERR_ACTION_QUEUE_FULL` from this block.
- The id comes from `in[0]` or the constant (1..255). Private state: 4 bytes: `u8 scope` (`VM_ACTION_SCOPE_STATIC` 0 / `_RECORDED` 1, the same values as `SYS_ACTION_SCOPE_*`), `u8 action_id`, `u8 flags` (runtime).

### ON_EVENT

`VM_BLK_ON_EVENT` is the "triggers are blocks" half of [[VM_EXEC.MD]]: it starts
a chain when a matching system event arrived this pass.

- It walks this pass's routed events (`vm_event_count()` / `vm_event_at()`), matching domain, device, channel and event — each exact or `VM_EVENT_ANY` (255, the same wildcard as `SYS_EVENT_ANY`).
- On a match: ENO pulses for this pass (loud), VALUE gets the event's `value` (converted to the output's type), COUNT the number of matches this pass. Several matches in one pass: VALUE is the last one's.
- Nothing is consumed; every ON_EVENT block sees every event of the pass. An event lives one pass, so one arriving while the block is disabled is missed, like a physical edge.
- Events reach the VM only through a subscription routed to it (class `0x09`, route `CONFIG_SYS_EVENT_ROUTE_VM`, [[SYS_EVENT.MD]]); the block doesn't subscribe. The app is expected to create that subscription when it uploads a program with ON_EVENT blocks.
- The output is a one-pass pulse; event → wait → act puts a `LATCH` below it.
- Private state: 4 bytes: `u8 domain`, `u8 device_id`, `u8 channel`, `u8 event`.

### Retained values (`core/retain/`)

An object uploaded with the `retentive` flag keeps its value across a reboot or
a program re-upload, like a PLC retain variable.

- **Kept by name.** A retentive object must be named (`ERR_VM_RETAIN_UNNAMED` at
  load). An edited program keeps a value as long as the name, type and size
  still match; a mismatch keeps the uploaded value and reports
  `ERR_VM_RETAIN_MISMATCH`. Stored names the program no longer has are dropped
  at the next save. `PTR` objects can't be retentive; CLONE's heap copies never are.
- **Saved** every `CONFIG_VM_RETAIN_PERIOD_S` (60 s) at the end of a completed
  pass, if anything changed: the VM task captures all retentive objects into a
  staging buffer (one consistent snapshot) and the retain task writes the whole
  set as one `sys_settings` record (`vm_retain`) — flash is never written on the
  VM task, and one record means a power loss mid-write keeps the previous set.
  Also saved right before a program is torn down (`0x40`, `0x41`) and when runit
  enters the safe state (battery critical, device fault). An unplanned power
  loss loses at most one period.
- **Restored** on the first start after a load (`0x48` normal / scan / block
  mode), before the first pass, **without** marking the objects fresh — exactly
  like uploaded initial values.
- **Saving is only active between a start and a teardown**, so an unstarted or
  empty program never overwrites what is stored (after a reboot the store waits
  for the program to be loaded and started again).
- **Reset all:** `0x48` command `10` (`VM_EXEC_RETAIN_CLEAR`, `vm_retain_clear()`)
  erases the store and stops saving until the next start, so the next start
  begins from the uploaded values.
- **Size:** everything a program retains must fit `CONFIG_VM_RETAIN_MAX_BYTES`
  (1024; record = 2 + per object 4 + name + value), checked per object at load
  (`ERR_VM_RETAIN_TOO_BIG`). RAM cost is twice that.
- **Across reboots** the program itself is not stored yet (it lives in RAM), so
  retained values come back once the app re-uploads and starts it. Storing the
  program on the device is the next step.

### `upd`, and who clears it

The supervisor, at the **end of a pass** — not at the point of consumption
(which would break fan-out: two blocks reading one fresh object, and whichever
ran first steals the update). The sweep
respects `upd_resetable`, and covers dynamic objects as well as the registry.
Subscriptions are sampled *before* it, via `vm_exec_set_sample_hook()`.

### The block watchdog

One store per block, no clock reads: the supervisor publishes `(seq << 16) |
block_id` to a volatile, and a 20 ms `esp_timer` samples it. Same word twice
running means that block hung, and the error **names it** rather than just
resetting the chip. The sequence number is what distinguishes "still running"
from "running again next pass".

[[VM_EXEC.MD]] specifies a timer ISR here with a deferred report. This uses an
ordinary `esp_timer`, whose callback already runs at task level on core 0 — so
it keeps sampling while core 1 is stuck, and can build the error where it
detects it. The deferred hand-off would have been worse: a genuinely hung block
never reaches the freeze point that would have drained the flag.

### Events

**An event lives exactly one scan cycle.** A FreeRTOS queue and a snapshot:

```
sys_event route here      ->  [ queue ]  -- drained at the top of a pass -->
[ this cycle's events ]  <-  blocks query here, until the next drain
```

A pass therefore sees one unchanging snapshot of everything that arrived while
the previous pass was running, and whatever it does not act on is overwritten
at the next drain. Events **can** go unhandled, on purpose: a block that was disabled
when its event arrived simply misses it, which is the same answer a physical
edge gives.

The drain replaces a hand-rolled queue's entire apparatus — no per-entry
lifetime, no sequence numbers, no visibility watermark, no ordering maintained
across passes — and the cost of it going wrong is one lost event rather than a
corrupted stream.

**What is buffered is `sys_event_t`** — the system's own event, exactly as
[[SYS_EVENT.MD]] delivered it (`domain, device_id, channel, event, value`).
There is no VM event type, on purpose: a VM-side struct would mean a translation
step that has to be extended every time a new event domain appears. GPIO edges,
power faults, motor faults and BLE traffic are the whole point of this buffer,
and they already have a shape.

The VM plugs in as **route slot `CONFIG_SYS_EVENT_ROUTE_VM` (0)**:
`vm_event_route`, registered by runit (`runit_error_wiring_init`). An event
reaches it only through a subscription that names this route (the app's
subscribe packet, class `0x09`). It doesn't *handle* the event — it parks it
for one scan cycle and lets blocks decide.

**Blocks ask; nothing is delivered, and nothing is addressed to a block.** An
event block is not woken and not routed to. It walks `[0, vm_event_count())`
with `vm_event_at()`, recognises what it cares about from the event's fields,
and either acts or stands down. Nothing is consumed, so two blocks
watching the same GPIO both see the edge — there is no ownership to arbitrate
and no order to get wrong. A driver reports "GPIO 4 rose" knowing nothing about
the program; any number of blocks may act on it, or none.

**There is no locking of the VM's own.** The producer is the event task (core 0);
the consumer is the supervisor (core 1), and they genuinely run at once — but a
FreeRTOS queue already arbitrates that correctly, so there is nothing here to
get wrong. After the drain, this cycle's events live in an array no other task
can see, which makes every read a block does lock-free *by construction* rather
than by argument.

`vm_event_post()` is **not** called from an ISR, despite being the far end of
an interrupt. `sys_event_publish()` already absorbed that with
`xQueueSendFromISR`, and the event task dispatches route functions at task
level, so ISR safety was handled a layer down. It never waits, though: a
producer that blocked would stall the event task and with it every other listener —
an event the VM has no room for must not cost `sys_io` its own delivery.

An earlier version hand-rolled a double buffer under a `portMUX`. It worked,
but it was two buffers, an index, a spinlock and a swap-time race to reason
about, reimplementing what `R_QUEUE_DEFINE` gives for free.

Overflow — more than `VM_EVENT_DEPTH` in one cycle — drops the newest and
raises `ERR_VM_EVENT_OVERFLOW`. Events dropped by the *drain* for want of a
taker are normal, and silent.

### User write protection

`mutable` permits internal writes; `usr_protected` additionally denies user
writes. Block construction sets protection on outputs and ENO. SET and CLONE
use `vm_block_obj_*` APIs, which check each written object's policy before mutation.
Producer blocks use internal APIs. Protection is object-local, so separately
allocated children need their own flag if they are also producer-owned.

Protection uses previously reserved bit 6 in the header flag byte. Existing
defined wire bits and the four-byte header are unchanged. The construction
helper accepts `VM_OBJ_F_USR_PROTECTED`; zero means no extra user restriction.
Initialization packets remain load-only and are refused while execution runs;
they are not a runtime remote-write API.

---

## Program RAM Budget

The pool is **allocated per load**, at exactly the size the program declares in `0x41`, from internal DRAM. It holds **everything a program owns** — objects, accessors, blocks and all three registries. A 5 kB program costs 5 kB; the other ~43 kB a fixed reservation would have held stays with BLE and the heap. `VM_STORE_MAX_POOL` (128 kB) is only a sanity ceiling so a corrupt `total_size` is rejected cleanly rather than attempted.

| item | bytes |
| :--- | :--- |
| block | 16 (`vm_block_data_t`) + 4/input + 4/output + 4/enable source + own variables |
| object | 4 (head) + payload, 4-byte aligned, + tag bytes if named |
| accessor | 20 + 8 per chain index — shared between pins reading the same thing, so unshared is the worst case |
| registries | 4 per id, in each of the three id spaces |

Worked from the target workload — 100 blocks, 5 pins each, one 8-byte output apiece, 16 B of own variables, **no** accessor sharing:

| | | bytes |
| :--- | :--- | ---: |
| blocks | 100 × (20 + 20 + 4 + 16) | 6.0 k |
| outputs | 100 × (4 + 8) | 1.2 k |
| constants | 100 × (4 + 8) | 1.2 k |
| accessors | 500 × (20 + 8) | 14.0 k |
| registries | 200 obj + 500 acc + 100 blk ids | 3.2 k |
| | **total** | **25.6 k** |

That figure is a **reference for the client's own estimate**, not a device-side reservation — there is no fixed pool to size against. The client declares what its program needs in `0x41` and the device allocates exactly that, per load. Scaling the same shape to the ~300-block ceiling the target workload assumes lands near 77 kB.

A program that does not fit — past `VM_STORE_MAX_POOL`, or larger than the biggest contiguous block internal DRAM can currently give — fails its load with `ERR_VM_LOAD_TOO_BIG` carrying requested-vs-available. Fragmentation is the real limit rather than free-heap total, which is why the error reports the largest available block: a client can tell "no room at all" from "no room in one piece".

Note the app **partition** is 1 MB (`CONFIG_PARTITION_TABLE_SINGLE_APP`), not the board's 16 MB flash (`sdkconfig` still declares 2 MB; see the runit-esp build notes). Code size is bounded by that partition: the whole firmware image was ~710 kB of 1 MB on 2026-09-23.

---

## Design Notes & Known Limitations

* **No test target.** The on-target self-test suite and the access benchmark were deleted in commit `47207d3`; the performance figures they produced are kept as history in [VM_PERF.MD](VM_PERF.MD).
* **Palette:** see "What ships today" — 15 block types, published for the app in `data-structures/vm/vm-blocks.generated.json`.
* **Retained values** are kept by name ("Retained values"); the program itself is not stored on the device yet.
* **Hardware Drivers Boundary:** Blocks in `blocks/` interact with runtime registries and accessors; hardware drivers (`sys_io`, `sys_power`) are invoked via standard system interfaces.
* **Timing & Clocks:** `vm_now_ms()` latches the pass timestamp per scan cycle, driving time-aware blocks such as `TIMER`.
* **Non-scalar Movement:** `SET` (`vm_block_set.h`) performs deep copy (`vm_obj_copy_content()`) across matching schemas with pairwise pointer array traversal.
* **Subscription & Telemetry:** Built via `vm_sub.h`/`.c`, maintaining object subscriptions and emitting telemetry on freshness updates.
* **Unwired Pins:** Unwired input pins map to NULL accessor slots (`VM_BLOCK_NO_ID` on wire), falling back to block constants stored in `custom_data`.

---

## API Summary

**Construction** — `vm_obj_create()`, `vm_obj_shape()`, `vm_obj_init()`, `vm_accessor_create()`, `vm_accessor_set_literal()`, `vm_accessor_set_ref()`, `vm_accessor_set_name()`, `vm_accessor_cache_build()`, `vm_block_create()`

**Dynamic objects** — `vm_obj_dyn_create()`, `vm_obj_dyn_retain()`, `vm_obj_dyn_release()`, `vm_obj_dyn_reset()`, `vm_obj_dyn_get_id()`, `vm_obj_dyn_get_by_id()`, `vm_obj_is_dynamic()`. Reference counts move in exactly one place — a pointer slot, via `slot_store()` in `vm_obj_access_internal.c` — and `vm_obj_dyn_get_id()` tests the `dynamic` bit before scanning, so an arena object costs one bit rather than `VM_DYN_MAX` pointer compares.
**Header helpers** — `vm_obj_get_total_size()`, `vm_obj_get_payload_size()`, `vm_obj_get_type_size()`, `vm_type_width()`, `vm_obj_get_items_cnt()`, `vm_obj_get_tag()`, `vm_obj_get_payload_ptr()`
**Store** — `vm_store_open()`, `vm_store_alloc()`, `vm_store_undo()`, `vm_store_get()`, `vm_store_reset()`, `vm_store_used()`, `vm_store_capacity()`
**Access** — `vm_obj_get_by_id()`, `vm_accessor_get_by_id()`, `vm_obj_get_payload()`, `vm_obj_get_obj()`, `vm_internal_set_scalar_slot()`, `vm_internal_set_scalar_direct()`, `vm_obj_copy_content()`, `vm_obj_copy_direct()`, `vm_obj_shape_matches()`, `vm_obj_schema_matches()`, `vm_obj_clone_shape()`, `vm_obj_clone_into()`, `vm_obj_link()`, `vm_obj_link_direct()`, `vm_obj_clear_quiet()`, `vm_payload_get_at()`, `vm_make_payload()`
**User mutation** (require `mutable` and reject `usr_protected`) — `VM_OBJ_SET_SCALAR_USR()`, `VM_OBJ_SET_SCALAR_AT_IDX_USR()`, `vm_block_obj_copy_content()`, `vm_block_obj_copy_direct()`, `vm_block_obj_clone_into()`, `vm_block_obj_link()`, `vm_block_obj_mark_updated()`
**Macros** — `VM_OBJ_SCALAR_GET()`, `VM_OBJ_SET_SCALAR()`, `VM_OBJ_SET_SCALAR_AT_IDX()`, `VM_OBJ_SET_SCALAR_USR()`, `VM_OBJ_SET_SCALAR_AT_IDX_USR()`, `VM_OBJ_COPY_CONTENT()`, `VM_PAYLOAD_GET_VAL()`, `VM_INTERNAL_LOAD_SCALAR()`
**Blocks** — `vm_block_get_by_id()`, `vm_block_get_in()`, `vm_block_get_out()`, `vm_block_is_enabled()`, `vm_block_triggered()`, `vm_block_input_fresh()`, `vm_block_set_eno()`, `vm_block_get_span()`, `vm_block_claim_span()`, `vm_block_get_inputs()`, `vm_block_get_outputs()`, `vm_block_get_en_list()`, `vm_block_get_custom_data()`, `vm_block_get_custom_len()`, `vm_block_get_total_size()`, `vm_block_calc_size()`, `vm_block_report_error()`, `vm_block_mark_failed()`, `vm_block_failed()`, `IF_BLOCK_ENABLED()`, `IF_BLOCK_TRIGGERED()`, `BLOCK_CALL()`
**Dispatch** — `vm_block_type_of()`, `vm_block_fn_for()`, `vm_block_verify()`, `vm_exec_check_block_type()`
**Events** — `vm_event_post()`, `vm_event_count()`, `vm_event_at()`, `vm_event_drain()`, `vm_event_take_overflow()`, `vm_event_reset()`
**Retained values** — `vm_retain_init()`, `vm_retain_reserve()`, `vm_retain_restore()`, `vm_retain_on_pass()`, `vm_retain_save_stopped()`, `vm_retain_clear()`, `vm_retain_on_open()`, `vm_retain_on_reset()`
**Execution** — `vm_exec_start()`, `vm_exec_request_stop()`, `vm_exec_stop()`, `vm_exec_fault_latch()`, `vm_exec_fault_status()`, `vm_exec_fault_acknowledge()`, `vm_exec_program_lock()`, `vm_exec_program_unlock()`, `vm_exec_pass()`, `vm_exec_run_range()`, `vm_exec_set_mode()`, `vm_exec_mode()`, `vm_exec_status()`, `vm_exec_pass_count()`, `vm_exec_last_pass_us()`, `vm_exec_set_sample_hook()`, `vm_exec_register_action_request()`, `vm_exec_request_action()`, `vm_exec_reset()`, `vm_clock_us()`, `vm_now_ms()`
**Loading** — `vm_loader_reset()`, `vm_loader_open()`, `vm_loader_add_obj()`, `vm_loader_set_data()`, `vm_loader_add_accessor()`, `vm_loader_add_block()`, `vm_loader_state()`, `vm_loader_used()`

---

## Error Ownership

Owners `0xA900`–`0xA908` (`SYS_VM_OWNER_MAP` in `errors/sys_error_vm.h`): `OWNER_VM_BASE`, `OWNER_VM_STORE`, `OWNER_VM_OBJ`, `OWNER_VM_ACCESSOR`, `OWNER_VM_BLOCK`, `OWNER_VM_CODE`, `OWNER_VM_LOADER`, `OWNER_DEC_VM_LOADER`, `OWNER_VM_EXEC`.

Tags (`SYS_ERROR_VM_MAP`) cover allocation (`ERR_VM_ALLOC_EXHAUSTED`), resolution (`ERR_VM_ACCESSOR_UNKNOWN_ID`, `_OOB`, `_TYPE_MISMATCH`, `_NULL_OBJ`, `_DEPTH_EXCEEDED`, `_INDEX_FAILED`, `_NAME_NOT_FOUND`, `_NOT_MUTABLE`), blocks (`ERR_VM_BLOCK_INPUT_UNRESOLVED`, `_PIN_MISSING`, `_PIN_UNLINKED`, `_FAILED`), construction (`ERR_VM_OBJ_BAD_TYPE`, `_EMPTY`, `_TOO_LARGE`, `_NAME_TOO_LONG`, `_RETENTIVE_PTR`, `_COPY_MISMATCH`), loading (`ERR_VM_LOAD_BAD_STATE`, `_TOO_BIG`, `_DATA_RANGE`, `_SHORT_RECORD`), accessors (`ERR_VM_ACC_INDEX_OOB`, `ERR_VM_ACC_BAD_KIND`), blocks (`ERR_VM_BLK_BAD_SHAPE`, `ERR_VM_BLK_BAD_REF`) and the registries (`ERR_VM_REG_OOB`, `ERR_VM_REG_DUP` — one pair for all three id spaces, with `kind` naming which).

Execution adds its own group: the palette (`ERR_VM_BLK_UNKNOWN_TYPE`), the
supervisor (`ERR_VM_EXEC_BLOCK_HUNG`, `ERR_VM_EXEC_SPAN_DEPTH`,
`ERR_VM_EXEC_BAD_SPAN`, `ERR_VM_EXEC_CONTROL`,
`ERR_VM_EXEC_SELF_BARRIER`, `ERR_VM_EXEC_FAULT_LATCHED`), events (`ERR_VM_EVENT_OVERFLOW`), runtime override
validation (`ERR_VM_OVERRIDE_PTR_UNSUPPORTED`,
`ERR_VM_OVERRIDE_BAD_RECORD`), and the user-write guard
(`ERR_VM_OBJ_USR_PROTECTED`). Retained values add `ERR_VM_RETAIN_UNNAMED`, `_TOO_BIG`, `_MISMATCH`, `_CORRUPT` (owner `OWNER_VM_RETAIN`, `0xA909`). Blocks add `ERR_VM_EXPR_BAD_CODE`, `ERR_VM_EXPR_MATH`, `ERR_VM_FOR_BAD_LOOP`, `ERR_VM_PERIODIC_OVERRUN`, and `ERR_VM_NO_ACTION_EXECUTOR` (an ACTION block with no request function registered).

`sys_error_vm.h` also supplies `SE_*_OWNED` variants (`SE_RAISE_OWNED`, `SE_ERR_NEW_OWNED`, `SE_FAIL_OWNED`, `SE_WRAP_ERR_OWNED`, `SE_CHECK_NOT_NULL_OWNED`, `SE_CHECK_IF_ALLOCATED_OWNED`) that take the owner explicitly, for header-only code pulled into translation units with their own ambient `OWNER`.

Because `sys_errors` aggregates this map, it reaches `VM/errors/sys_error_vm.h` through a raw include path rather than `REQUIRES VM` — VM already requires `sys_errors`, so a component dependency back would be circular. The `errors/` folder holds only the map, so the VM API doesn't leak (see [[SYS_ERRORS.MD]] "Error maps and aggregation").

