# VM blocks — palette and recipe

## 1. Palette (`blocks/vm_blocks.h`, `vm_blocks_table.c`)

Type 0 is reserved (unset type must not run). The type byte indexes `g_vm_block_types[]`: one `vm_block_type_t` per type (body, extra check, `min_in` / `max_in`, `min_q` / `max_q`, `min_en`, `required_in`, `state_len`), **generated** from the block's `//#vm-block` directives into `blocks/vm_blocks_registry.generated.h`. The app reads the same data from `data-structures/vm/blocks/` (one descriptor per block).

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
| 10 | `IO_SET_LEVEL` | 0 level (optional, `default_level` while unwired), 1 pin | — | 16 (`allowed_mask u64`, `device_id`, `default_io_num`, `when_not_active` LOW/HIGH/HOLD, runtime `flags`, `last_pin`, `default_level` LOW/HIGH) | enabled; writes on change only |
| 11 | `IO_TOGGLE` | 0 pin | — | 16 (`allowed_mask`, `device_id`, `default_io_num`, flags) | rising edge of enable |
| 12 | `LATCH` (SR / RS) | 0 set, 1 reset (≥1 wired) | 0: Q (written on change) | 4 (mode, flags) | enabled; ENO = Q level; disabled holds Q |
| 13 | `PERIODIC` | 0 period (fallback constant, `time_base` units; hidden until wired) | — (ENO = the tick) | 16 (period, time base, flags, next deadline) | enabled; one-pass tick per period, no burst, overrun reported once |
| 14 | `ACTION` | 0 action id | — | 4 (scope, id, flags) | rising edge of enable; **queues** the action (`vm_exec_request_action`) |
| 15 | `ON_EVENT` | — | 0: value, 1: count | 4 (domain, device, channel, event; 255 = any) | enabled **and** a matching event this pass (`vm_event_at`) |

ENO is optional on every block (`VM_BLOCK_NO_ID` on the wire). Per-block layouts and semantics: the block's own header (diagram + `custom_data layout` comment) and VM.MD "What ships today" plus the per-block sections after it.

Hardware blocks call `sys_io_*` with `SYS_IO_REF(device_id, pin)`. `allowed_mask` restricts which pins a *dynamic* pin input may select; it is the program's own declaration, not a security boundary — protection of board pins is `sys_io`'s (locked pins, onboard devices).

## 2. Adding a block type

1. **Folder** `blocks/<name>/` with `vm_block_<name>.h` (the public part: state struct, enums, pin enums, prototypes and the `//@` directives next to them; `#include "vm_block_helpers.h"` plus the `sys_*` it drives), `vm_block_<name>.c` (the body and the check), `<name>.display.json` (the face: title, category, description, pin titles, written by hand), `<name>.content.json` (generated from the header: never edited) and `<name>.md` (the user guide, copied to `app/docs/blocks/`). Header comment: ASCII pin diagram and short developer notes; the `custom_data` layout is the struct (the generator checks it), the behaviour for users goes in the guide. One block per folder; code two blocks share goes in its own folder without a `//#vm-block` (`expr_core/`, `branch_core/`).
2. **Private state**, if any: one struct, `__attribute__((aligned(4 or 8)))` (the block reads and writes it with `memcpy`, since `custom_data` is only 4-byte aligned), explicit `_pad`, `_Static_assert(sizeof(...) == N)` and `offsetof` asserts for fields the app writes; `#define VM_<NAME>_CUSTOM_LEN sizeof(...)`. Runtime-only bytes (flags, prev values, `rt`) are zero on the wire. A span owner puts `vm_span_t` first.
3. **Pins**: enum `vm_in_<name>_e` (and `vm_out_<name>_e`) marked `//#block-enum`, one member `VM_IN_<NAME>_<PIN>` per pin, each with a trailing `//@in @value <kind> [@required] [@overrides <field>]` (outputs `VM_OUT_...` / `//@out`). A pin that repeats (any number) is the last member, `VM_IN_<NAME>_<PIN>_ANY` with `@repeat`. The C reads pins by these members.
4. **Check** (optional) `bool vm_verify_<name>(vm_block_h b)` (declared in the header, defined in the `.c`): only what the directives can't express — enum / range fields, a constant needed when a pin is unwired. Pin counts (min and max), required pins, enable sources and state size are checked from the table entry, which the directives generate: do not repeat them here. Runs once at load; read the state with `memcpy`. No reporting: the builder returns `ERR_VM_BLK_BAD_SHAPE` and the load is aborted as a whole (the program is discarded), so a rejected block never runs.
5. **Body** `void vm_blk_<name>(vm_block_h b)` (declared in the header, defined in the `.c`), template:

   ```c
   static inline void vm_blk_foo(vm_block_h b) {
     vm_foo_data_t st;                                   // shape was checked at load: no runtime re-check                                   // custom_data is only 4-aligned: copy in/out
     memcpy(&st, vm_block_get_custom_data(b), sizeof(st));
     vm_foo_data_t* d = &st;

     IF_BLOCK_TRIGGERED(b) IF_BLOCK_ENABLED(b) {        // pick the activation this block needs
       float x = 0;
       if (!vm_block_check(b, VM_BLOCK_GET_PARAM(x, b, VM_IN_FOO_X, d->k_x))) {  // optional pin, constant fallback
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

6. **Catalog and shape**: the directives stand next to what they describe (no `VM_BLOCK_TYPE_*` macro: the table entry is generated). `//@data <struct>` above the struct, `//@rule <text> @error <ERR>` above `vm_verify_<name>`, and above the body prototype `//#vm-block VM_BLK_<NAME> @id <next free id>` with `//@activation <kind>` (and `//@enables required` when it needs an enable source). `@required` on a pin member marks one the block cannot run without; the numbered pins set the maximum:

   ```c
   //#block-enum @alias Foo Inputs
   typedef enum vm_in_foo_e {
     VM_IN_FOO_X = 0,  //@in @value f32 @overrides gain
   } vm_in_foo_e;
   //#block-enum @alias Foo Outputs
   typedef enum vm_out_foo_e {
     VM_OUT_FOO_Y = 0,  //@out @value f32 @required
   } vm_out_foo_e;

   bool vm_verify_foo(vm_block_h b);
   //#vm-block VM_BLK_FOO @id <next free id>
   //@activation triggered
   void vm_blk_foo(vm_block_h b);
   ```

   The face goes in `foo.display.json` (`title`, `category`, `description`, `activation` text, `header` such as `Foo | {x} {unit}` whose `{ref}`s name pins and settings, and a `title` / `description` per pin by pin name). Enums the state uses get `//#block-enum` (a block's own) or `//#ref-enum` (shared by two blocks); state fields get `@description`, `@enum-ref`, `@runtime` (device-owned). A pin with a constant to fall back on (`VM_BLOCK_GET_PARAM(out, b, pin, state.field)`) says `@overrides <field>`: the app hides the pin and lets the user type the constant. The app renders the block from `display.json` + `content.json` alone, so a new block needs no app code. Grammar: `data-structures/auto-annotations/vm/vm-annotations.md`.
7. **Register**: nothing to edit. The block's id is the `@id` of its `//#vm-block` line (1 to 255, unique, never reused for another block: it is the `block_type` on the wire); the generator writes `vm_block_ids.generated.h` (the `VM_BLK_FOO` number) and `vm_blocks_registry.generated.h` (the include and the table entry), both committed. Do not edit them or `vm_blocks.h` / `vm_blocks_table.c`.
8. **Generate + docs**: run `idf.py reconfigure` (the block folders are globbed at configure time) and `python data-structures/auto-annotations/generate-all.py` (the block generator fails on a stray or unknown `//@` line, a wrong struct size or offset, a pin member without its `//@in`, a pin enum value that leaves a gap, a face directive in the header, an unknown `{ref}` or an unknown enum) and `--check --test`; update VM.MD's palette table and block section, and this table.

### Activation patterns

| Block kind | Test | Example |
|---|---|---|
| Function of its inputs | `IF_BLOCK_TRIGGERED(b) IF_BLOCK_ENABLED(b)` | EXPR |
| Copy / event on one source pin only | `vm_block_triggered_by(b, SRC)` then enabled | SET, CLONE (a trigger over the dst pin would re-fire forever) |
| Level / actuator | `IF_BLOCK_ENABLED(b)`; define what "disabled" does | IO_SET_LEVEL (`when_not_active`), TIMER |
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

Faults: a malformed configuration is rejected at load (the palette `.check`), EXPR bytecode included (`vm_expr_check`, reported as `ERR_VM_EXPR_BAD_CODE`); bodies never re-check it. A bad **value** (zero divisor, non-finite) is reported per fault episode and re-armed by the next clean run. Never report every pass: at 100 Hz that buries the log.

Publishing: every block needs `@activation`, a `@value` on each pin member and a `//@rule` per check its `.check` makes (vm-annotations.md "Block catalog"); regenerate with `generate-vm-blocks.py`.
