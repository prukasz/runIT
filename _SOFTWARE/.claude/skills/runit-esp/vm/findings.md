# VM — open work

From the review of `components/VM` on 2026-09-23. Fixed items are in PROGRESS.md "Done"; this file keeps only what is still open. Delete an item here when it lands.

## 1. Open gaps

| # | Gap | Why it matters | Direction |
|---|---|---|---|
| G-5 | **No test target in the repo** (on-board black-box tests over the UART link exist since 2026-09-24: build.md §6, `vm_tests.py`). A host build works (tried 2026-09-23): `zig cc` from the pip package `ziglang` (installed into a temp dir, nothing system-wide), `-target x86-windows-gnu` — 32-bit, because `vm_obj.h` asserts 4-byte pointers. The real `sys_error.c` and VM `errors/`, `store/`, `obj/`, `sub/` compile with small stubs (ESP log/err/compiler/heap_caps, FreeRTOS `portMUX`, `DBG`, data connector, `vm_exec` mode/lock). A one-off `vm_sub` suite (13 tests, frames decoded into an app model and checked after every pass, random stress over 60 seeds) found two telemetry bugs, both fixed; the test files were deleted afterwards. | ~9 k lines of hot-path code with untrusted input and no regression net. | Decide whether to keep host tests in the repo (e.g. `test/host/` with the stubs and a build script; extending to `exec/` + blocks needs `esp_timer` / task shims), or use an ESP-IDF Unity test app run on the board. |
| G-10 | **PERIODIC reports an overrun after every pause.** Seen on the devkit 2026-09-24: `0x48 06` pause, then `07` resume → `ERR_VM_PERIODIC_OVERRUN` (missed 2, period 500 ms). The periods that passed while paused count as dropped ticks. | A deliberate pause shows up as an error in the app. | Re-arm the deadline (like a disable / enable) when the VM resumes from pause, or skip the report when the gap covers a pause. |
| G-11 | **Triggered blocks ignore enables as a trigger.** EXPR, EXPR_BIT, SET and CLONE run only when an input is fresh (`vm_block_triggered`), and an enable source is not an input. An EXPR whose inputs are user objects (not fresh after load) and that is enabled by an IF branch never computes. Seen twice on the PCB 2026-09-27 (the accessor and program samples needed a PERIODIC tick wired as an extra input) | The obvious canvas pattern "compute this while the branch is active" silently does nothing | Proposed (owner to decide, `runit-app/features.md` §8): a fresh enable source also triggers (gates and ticks are written loud while true). Alternative: the compiler adds each EN source as a hidden input |
| G-12 | **EN-rising blocks with no enables act once at start.** ACTION and IO_TOGGLE remember the previous enable (`*_F_PREV_EN`, 0 after load); no sources = enabled, so the first pass is a rising edge and they act once, then never again | An unwired "Do" looks idle but fires at program start | App warns ("runs once when the program starts"), or make "on start" an explicit option |
| G-13 | **EDGE threshold is per pass.** `vm_edge_step` compares with the previous pass (`prev_val` updated every pass), so `change_by` 5 fires only on a change of 5 within one pass; a counter rising 1 per tick never fires (program samples, 2026-09-27) | The block description ("changes by at least the threshold") reads as a change since the last pulse | At least say "within one pass" in `//@block-description`; or keep `prev_val` until it fires (a design change) |

## 2. Closed decisions

- G-1 (block catalog): done — one descriptor per block in `data-structures/vm/blocks/` from `//#vm-block` + the palette entry macros.
- G-2 (app contract): done — `vm-program.generated.json` (records, telemetry, widths, limits, arena), per-block descriptors with pins, activation and load rules, EXPR / EXPR_BIT bytecode (opcode table, load-time check, golden vectors checked on the real evaluator), the FOR span as a derived field.
- G-3 (event block): done — `ON_EVENT`.
- G-4 (latch / periodic / action): done.
- G-8 (program stored on the device) and G-9 (flash size, room for programs): done 2026-09-26 — the program is part of the board's stored code (`sys_project`, own 512 KB `project` NVS partition, 16 MB flash), replayed at boot, autostart from `prj_opts`.
- G-7 (retained values): done — kept by name, saved every 60 s off the VM task, restored on the first start, `0x48 0A` forgets them.
- G-6 (accessor by-ref recursion depth): accepted as is — bounded by `CONFIG_VM_ACCESSOR_MAX_DEPTH`.
- Fault flag: the global `g_vm_block_fault` became the per-call bit `VM_BLK_RT_FAULT`.
- `@verified` markers were removed; they carry no meaning.
