# VM — open work

From the review of `components/VM` on 2026-09-23. Fixed items are in PROGRESS.md "Done"; this file keeps only what is still open. Delete an item here when it lands.

## 1. Open gaps

| # | Gap | Why it matters | Direction |
|---|---|---|---|
| G-2 | **Class `0x04` record layouts not published** (the block catalog and `vm_exec_command_e` are). | The app hand-codes the upload records (`0x41`–`0x48`). | Describe the packets in JSON; variable-length records need the trailing-array support the settings generator already has. Do with the app sync. |
| G-5 | **No test target.** No host C compiler is installed on this machine (esp-clang has no host libc/linker), so a host build of `core/` isn't possible as is. | ~9 k lines of hot-path code with untrusted input and no regression net. | Decide: install a host toolchain (e.g. LLVM-MinGW or Zig) for a host test build of `core/` + blocks with FreeRTOS/esp_timer shims, or an ESP-IDF Unity test app run on the board. |
| G-8 | **The program itself isn't stored on the device.** | After a reboot nothing runs, and retained values wait until the app re-uploads the program. | Next step (agreed): store the uploaded program in flash and load it at boot. |
| G-9 | **NVS partition is the default 24 KB and `sdkconfig` declares 2 MB flash** (board: 16 MB). | Retained values, settings and recorded actions share it; storing programs will need much more. | Fix the flash size, enlarge NVS (64–128 KB) or give programs their own partition. |

## 2. Closed decisions

- G-1 (block catalog): done — `vm-blocks.generated.json` from `//#vm-block` + the palette entry macros.
- G-3 (event block): done — `ON_EVENT`.
- G-4 (latch / periodic / action): done.
- G-7 (retained values): done — kept by name, saved every 60 s off the VM task, restored on the first start, `0x48 0A` forgets them.
- G-6 (accessor by-ref recursion depth): accepted as is — bounded by `CONFIG_VM_ACCESSOR_MAX_DEPTH`.
- Fault flag: the global `g_vm_block_fault` became the per-call bit `VM_BLK_RT_FAULT`.
- `@verified` markers were removed; they carry no meaning.
