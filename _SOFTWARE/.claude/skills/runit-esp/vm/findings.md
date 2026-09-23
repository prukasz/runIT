# VM — open work

From the review of `components/VM` on 2026-09-23. Fixed items are in PROGRESS.md "Done"; this file keeps only what is still open. Delete an item here when it lands.

## 1. Open gaps

| # | Gap | Why it matters | Direction |
|---|---|---|---|
| G-5 | **No test target in the repo.** A host build works (tried 2026-09-23): `zig cc` from the pip package `ziglang` (installed into a temp dir, nothing system-wide), `-target x86-windows-gnu` — 32-bit, because `vm_obj.h` asserts 4-byte pointers. The real `sys_error.c` and VM `errors/`, `store/`, `obj/`, `sub/` compile with small stubs (ESP log/err/compiler/heap_caps, FreeRTOS `portMUX`, `DBG`, data connector, `vm_exec` mode/lock). A one-off `vm_sub` suite (13 tests, frames decoded into an app model and checked after every pass, random stress over 60 seeds) found two telemetry bugs, both fixed; the test files were deleted afterwards. | ~9 k lines of hot-path code with untrusted input and no regression net. | Decide whether to keep host tests in the repo (e.g. `test/host/` with the stubs and a build script; extending to `exec/` + blocks needs `esp_timer` / task shims), or use an ESP-IDF Unity test app run on the board. |
| G-8 | **The program itself isn't stored on the device.** | After a reboot nothing runs, and retained values wait until the app re-uploads the program. | Next step (agreed): store the uploaded program in flash and load it at boot. |
| G-9 | **NVS partition is the default 24 KB and `sdkconfig` declares 2 MB flash** (board: 16 MB). | Retained values, settings and recorded actions share it; storing programs will need much more. | Fix the flash size, enlarge NVS (64–128 KB) or give programs their own partition. |

## 2. Closed decisions

- G-1 (block catalog): done — one descriptor per block in `data-structures/vm/blocks/` from `//#vm-block` + the palette entry macros.
- G-2 (app contract): done — `vm-program.generated.json` (records, telemetry, widths, limits, arena), per-block descriptors with pins, activation and load rules, EXPR / EXPR_BIT bytecode (opcode table, load-time check, golden vectors checked on the real evaluator), the FOR span as a derived field.
- G-3 (event block): done — `ON_EVENT`.
- G-4 (latch / periodic / action): done.
- G-7 (retained values): done — kept by name, saved every 60 s off the VM task, restored on the first start, `0x48 0A` forgets them.
- G-6 (accessor by-ref recursion depth): accepted as is — bounded by `CONFIG_VM_ACCESSOR_MAX_DEPTH`.
- Fault flag: the global `g_vm_block_fault` became the per-call bit `VM_BLK_RT_FAULT`.
- `@verified` markers were removed; they carry no meaning.
