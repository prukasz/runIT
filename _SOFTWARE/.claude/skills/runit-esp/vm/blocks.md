# VM blocks — palette and recipe

## 1. Palette (`blocks/vm_blocks.h`, `vm_blocks_table.c`)

Type 0 is reserved (unset type must not run). The type byte indexes `g_vm_block_types[]`: one `vm_block_type_t` per type (body, extra check, `min_in`, `min_q`, `required_in`, `state_len`), defined in the block's own header as `VM_BLOCK_TYPE_<NAME>`. The app reads the same data from `data-structures/vm/vm-blocks.generated.json`.

| Id | Type | Pins in (★ required) | Out | `custom_len` | Activates on |
|---|---|---|---|---|---|
| 1 | `EXPR` (float RPN) | any, named by code (`IN n`) | ≥1: result | header 4 B + `u32 consts[]` + code | triggered **and** enabled |
| 2 | `EXPR_BIT` (u32 RPN) | 〃 | 〃 | 〃 | 〃 |
| 3 | `IF` | ★0 condition | ≥2: yes/no (flow) | 0 | enabled |
| 4 | `SWITCH` | ★0 selector (rounded to int) | ≥1, ≤16 branches (flow) | 0 | enabled |
| 5 | `FOR` | 0 start, 1 end, 2 step (fallback: constants) | 0: iterator (loud each turn) | 24 (`vm_for_code_t`, span first) | enabled; **claims its span first, always** |
| 6 | `SET` | ★0 src (trigger), ★1 dst | — | 0 | src fresh **and** enabled |
| 7 | `CLONE` | ★0 src (trigger), ★1 dst pointer cell | — | 0 | src fresh **and** enabled; heap only on schema change |
| 8 | `EDGE` | ★0 signal, 1 threshold | 0: pulse (1 pass) | 12 | enabled (re-syncs on re-enable) |
| 9 | `TIMER` (TON/TOF/TP + inverted) | ★0 IN, 1 PT | 0: Q, 1: ET | 32 (mode, flags, time base ms/s/min/h, pt, start, elapsed) | enabled (disabled = stand down, clear) |
| 10 | `IO_SET_LEVEL` | ★0 level, 1 pin | — | 16 (`allowed_mask u64`, `device_id`, `default_io_num`, `disabled_action` HOLD/LOW/HIGH, flags, `last_pin`) | enabled; writes on change unless `F_ALWAYS` |
| 11 | `IO_TOGGLE` | 0 pin | — | 16 (`allowed_mask`, `device_id`, `default_io_num`, flags) | rising edge of enable |
| 12 | `LATCH` (SR / RS) | 0 set, 1 reset (≥1 wired) | 0: Q (written on change) | 4 (mode, flags) | enabled; ENO = Q level; disabled holds Q |
| 13 | `PERIODIC` | 0 period (fallback constant, `time_base` units) | 0: tick | 16 (period, time base, flags, next deadline) | enabled; one-pass tick per period, no burst, overrun reported once |
| 14 | `ACTION` | 0 action id | — | 4 (scope, id, flags) | rising edge of enable; **queues** the action (`vm_exec_request_action`) |
| 15 | `ON_EVENT` | — | 0: value, 1: count | 4 (domain, device, channel, event; 255 = any) | enabled **and** a matching event this pass (`vm_event_at`) |

ENO is optional on every block (`VM_BLOCK_NO_ID` on the wire). Per-block layouts and semantics: the block's own header (diagram + `custom_data layout` comment) and VM.MD "What ships today" plus the per-block sections after it.

Hardware blocks call `sys_io_*` with `SYS_IO_REF(device_id, pin)`. `allowed_mask` restricts which pins a *dynamic* pin input may select; it is the program's own declaration, not a security boundary — protection of board pins is `sys_io`'s (locked pins, onboard devices).

## 2. Adding a block type

1. **Header** `blocks/vm_block_<name>.h`, `#include "vm_block_helpers.h"` (plus the `sys_*` it drives). Top comment: ASCII pin diagram, one-paragraph behaviour, and the `custom_data` byte layout.
2. **Private state**, if any: one struct, `__attribute__((aligned(4 or 8)))` (the block reads and writes it with `memcpy`, since `custom_data` is only 4-byte aligned), explicit `_pad`, `_Static_assert(sizeof(...) == N)` and `offsetof` asserts for fields the app writes; `#define VM_<NAME>_CUSTOM_LEN sizeof(...)`. Runtime-only bytes (flags, prev values, `rt`) are zero on the wire. A span owner puts `vm_span_t` first.
3. **Pin constants**: `#define VM_<NAME>_IN_<ROLE> n`, outputs likewise.
4. **Check** (optional) `static inline bool vm_verify_<name>(vm_block_h b)`: only what the table can't express — enum / range fields, at least one of two pins wired. Pin counts, required pins and state size are checked from the entry. Runs once at load; read the state with `memcpy`. No reporting: the builder returns `ERR_VM_BLK_BAD_SHAPE` and removes the block again (`vm_store_undo`), so a rejected block never runs.
5. **Body** `static inline void vm_blk_<name>(vm_block_h b)`, template:

   ```c
   static inline void vm_blk_foo(vm_block_h b) {
     vm_foo_data_t st;                                   // shape was checked at load: no runtime re-check                                   // custom_data is only 4-aligned: copy in/out
     memcpy(&st, vm_block_get_custom_data(b), sizeof(st));
     vm_foo_data_t* d = &st;

     IF_BLOCK_TRIGGERED(b) IF_BLOCK_ENABLED(b) {        // pick the activation this block needs
       float x = 0;
       if (!vm_block_check(b, VM_BLOCK_GET_PARAM(x, b, VM_FOO_IN_X, d->k_x))) {  // optional pin, constant fallback
         vm_block_set_eno(b, false);
         return;
       }
       BLOCK_CALL(VM_OBJ_SET_SCALAR_AT_IDX(x * 2.0f, vm_block_get_outputs(b)[0], 0), b);
       if (likely(!vm_block_failed(b))) {
         vm_block_set_eno(b, true);                        // loud
         return;
       }
     }
     vm_block_set_eno(b, false);                         // quiet: outputs stay, flow withdrawn
   }
   ```

6. **Palette entry + catalog**, at the end of the header:

   ```c
   //#vm-block VM_BLK_FOO @title Foo @category data @state vm_foo_data_t
   //@block-description What it does, for the app.
   //@in 0 x @title X @description ...
   //@out 0 y @title Y
   #define VM_BLOCK_TYPE_FOO \
     {.run = vm_blk_foo, .check = vm_verify_foo, .min_in = 1, .min_q = 1, .required_in = 0x1u, .state_len = VM_FOO_CUSTOM_LEN}
   ```

   Enums the state uses get `//#ref-enum`; state fields get `@enum-ref`, `@runtime` (device-owned), plain text = description. Grammar: `data-structures/auto-annotations/vm/vm-annotations.md`.
7. **Register**: `#define VM_BLK_FOO <next id>` in `vm_blocks.h`; include the header and add `[VM_BLK_FOO] = VM_BLOCK_TYPE_FOO,` to `vm_blocks_table.c`.
8. **Generate + docs**: run `generate-enums.py` and `generate-vm-blocks.py` (it fails on a missing directive, a wrong struct size or an unknown enum); update VM.MD's palette table and block section, and this table.

### Activation patterns

| Block kind | Test | Example |
|---|---|---|
| Function of its inputs | `IF_BLOCK_TRIGGERED(b) IF_BLOCK_ENABLED(b)` | EXPR |
| Copy / event on one source pin only | `vm_block_triggered_by(b, SRC)` then enabled | SET, CLONE (a trigger over the dst pin would re-fire forever) |
| Level / actuator | `IF_BLOCK_ENABLED(b)`; define what "disabled" does | IO_SET_LEVEL (`disabled_action`), TIMER |
| Edge of enable | keep `prev_en` in state | IO_TOGGLE, ACTION |
| Event | enabled, then scan `vm_event_count()` / `vm_event_at()` for a match | ON_EVENT |
| Periodic tick | deadline in state, `vm_now_ms()` | PERIODIC |
| Anything that must not run in a pass (system actions, long work) | hand it to another task through a registered function | ACTION (`vm_exec_request_action`) |
| Router (flow outputs) | enabled; drive taken branch loud, clear others with `vm_block_drive_gate(..., false)` (quiet); retract all on failure | IF, SWITCH |
| Span owner | `vm_block_claim_span()` **first and unconditionally**, then `vm_exec_run_range()`; check `vm_exec_cancelled()` after it returns | FOR |

### Helpers and their contracts (`vm_block_helpers.h`, `vm_block.h`)

| Helper | Does |
|---|---|
| `vm_block_failed(b)` / `vm_block_mark_failed(b)` | This call's fault bit (`VM_BLK_RT_FAULT` in `cfg.rt`, cleared before every call); `cfg.on_error` acts on it |
| `VM_BLOCK_GET_PARAM(out, b, pin, fallback)` | Optional pin: unwired → fallback, wired failure → `err_h` (preserves `out`) |
| `vm_block_check(b, err)` | Report `err` with block context; false on error |
| `BLOCK_CALL(call, b)` | Same as check for a call; marks the call failed |
| `vm_block_set_eno(b, s)` | True loud, false quiet; no-op without ENO |
| `vm_block_drive_gate(b, pin, s)` | Router output: true loud, false quiet |
| `vm_block_obj_*` (`copy_content`, `clone_into`, `link`, `mark_updated`) | **User mutation boundary**: rejects `usr_protected` targets; use for anything a program's data pins name as a destination |
| `VM_OBJ_SCALAR_GET` / `VM_OBJ_SET_SCALAR[_AT_IDX]` | Converting read / internal write (float→int rounds and saturates) |

Faults: a malformed configuration is rejected at load (verify). A malformed bytecode program (EXPR) is a **standing** condition (report once, latch `VM_BLK_RT_CFG_BAD`); a bad **value** (zero divisor, non-finite) is reported per fault episode and re-armed by the next clean run. Never report every pass: at 100 Hz that buries the log.
