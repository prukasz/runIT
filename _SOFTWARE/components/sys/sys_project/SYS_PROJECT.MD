# sys_project — the stored project (for agents)

Component `components/sys/sys_project/`. The pipeline that saves a whole **project** and runs it: the system settings, the devices' install and default commands, the user's actions and the **program** (VM loader frames, of any size). It sits on `sys_actions` (SYS_ACTIONS.MD): everything it stores is a record; it only decides which records, under which IDs, and in which order they run. Replaces legacy `sys_project` (two NVS slots, replayed at boot; `legacy/components/system/sys_project`).

## Files

| File | Role |
|---|---|
| `sys_project.h` | API, ID macro `SYS_PROJECT_ID`, flags, error tags, the info and mark structs |
| `sys_project.c` | The stream parser, staging, commit, run, info, erase, boot |
| `sys_project_router.c/.h` | `project` router (`ROUTER_KEY_PROJECT` = 0x0C), op IDs `SYS_PROJECT_OP_*` |
| `Kconfig` | Largest stream, sections, program chunks |

## The save: one stream

The app sends the project as **one stream**: frames back to back, `[u16 len LE][keys + payload]` (the image format of a record), cut into **sections**. A section opens with a SECTION frame `[0x0C][0x10][u32 id]`; the frames after it belong to it.

| Section id | The frames become |
|---|---|
| `!= 0` (class USER / SYSTEM / DEVICE, e.g. `0x01000001` log, `0x02000063` device 99) | the record under that ID, as they are, at COMMIT |
| `0` (`SYS_PROJECT_SECTION_PROGRAM`) | the program: cut into chunk records of whole frames (`CONFIG_SYS_ACTIONS_MAX_BYTES` at most), **numbered one after another by the pipeline** |

The program must be the **last** section (system, devices, then code: that is the run order); there is one. Sections are stored in the stream's order, each at most the limit of its class (a device record 512 bytes).

```
stream:  [SECTION 0x01000001][log SET frame]                       -> record 0x01000001
         [SECTION 0x02000000][CREATE, defaults ...]                -> record 0x02000000
         [SECTION 0][frame][frame]...(any number)                  -> chunks 1, 2, 3 ... and the list 0
```

### IDs one after another

Class `0x03` PROJECT (`SYS_ACTIONS_CLASS_PROJECT`), index `[bank+1 u8][n u16]` (`SYS_PROJECT_ID(bank, n)`). Two **banks** alternate: a save fills the one the stored project doesn't use, so the stored project's records are never touched. In a bank `n = 0` is the program's **list of chunks** (a record of `RUN <chunk>` frames, stops at a failing chunk), `n = 1, 2, 3 …` are given in the order they are needed: a program chunk, or a section waiting for COMMIT. `0x03000000` (`SYS_ACTIONS_ID_PROJECT`) is the **project record**.

### Commit = one store

Nothing a project changes is touched while the stream comes: every section is staged in the spare bank. COMMIT checks the length, the CRC (zlib) and the framing (a cut inside a frame is `ERR_PROJECT_TRUNCATED`), then:

1. stores the program's list of chunks;
2. copies every staged section to its own ID (`sys_actions_copy`, no flash write when it is the same image) and frees the staging record;
3. stores the **project record**: `[0x0C][0x11 MARK][u8 bank, u32 stream crc32, u32 stream length]`, then one `RUN_ALL <id>` frame per section in order, then `RUN <list>` for the program. **This is the commit point**: until it is stored, the old project is the stored one;
4. clears the other bank (the old program).

A failed or aborted save (`ABORT`, a write error, a bad CRC, the next `BEGIN` after a power cut) drops the staged records and leaves the stored project and the settings as they were. Only a flash failure *inside* step 2 can leave some settings new: they are separate records.

**Run.** BEGIN's `flags` bit 1 (`SYS_PROJECT_FLAG_RUN`) runs the project right after COMMIT; bit 0 (`SYS_PROJECT_FLAG_AUTOSTART`) sets the **autostart** option (kept in the `project` namespace of the actions partition, key `opts`, replaced by every save). A failing run is `ERR_PROJECT_RUN` wrapping the cause: the project **is stored** anyway.

`sys_project_run` = `sys_actions_run(project record, keep_going = true)`: the settings and devices run with `RUN_ALL` frames (one failing device leaves the rest configured), the program with `RUN` (stops at its first failing frame; the VM loader fails closed anyway). Depth: project record → list → chunk = 3 RUNs, so `CONFIG_SYS_ACTIONS_MAX_DEPTH` must be at least 3 (a `_Static_assert`).

**The VM start is the program's own last frame** (`[0x0B][0x08 EXEC]` normal): the firmware does not know the VM here. A program without it is loaded and stays stopped.

**Boot.** Last boot step `project autostart` (`sys_project_boot`): with a stored project and autostart on, it queues `[0x0C][0x06 RUN]` with `router_inject` (the run happens on a router task, after the devices are created). Without autostart nothing replays by itself, as everywhere else.

## Remote (`sys_project_router`, `ROUTER_KEY_PROJECT` = 0x0C)

| Op | Request | Response |
|---|---|---|
| `0x01` BEGIN | `u32 len, u8 flags` (1 autostart, 2 run after COMMIT) | – |
| `0x02` WRITE | `u32 offset`, data | – (offset = bytes so far; a wrong one is `ERR_PROJECT_OFFSET` and the save goes on, any parse error drops it) |
| `0x03` COMMIT | `u32 crc32` | – |
| `0x04` ABORT | – | – |
| `0x05` INFO | – | `u8 stored, u8 autostart, u8 bank, u32 crc32, u32 bytes` (crc and length of the stream it was saved from: the app compares them with its project) |
| `0x06` RUN | – | – (`ERR_PROJECT_NOT_STORED` with none) |
| `0x07` ERASE | – | – (the project record and both banks go; the settings, devices and user records stay) |
| `0x08` OPTIONS | `u8 autostart` | – |
| `0x10` SECTION | `u32 id` | only a frame of the stream: sent as a command it is `ERR_PROJECT_NOT_IN_STREAM` |
| `0x11` MARK | `u8 bank, u32 crc32, u32 bytes` | first frame of the project record; does nothing when run |

One save at a time (`ERR_PROJECT_BUSY`). Reading a project back is the actions router's READ (the project record, the list, the chunks); the app's own project is the source for recovery.

## Errors (`ERR_MOD_SYS_PROJECT` = 16)

`ERR_PROJECT_BUSY`, `_NO_SESSION`, `_TOO_BIG {len, max}` (also a stream longer than announced), `_OFFSET {got, want}`, `_INCOMPLETE {got, want}`, `_CRC {got, want}`, `_BAD_FRAME {offset}`, `_TRUNCATED`, `_NO_SECTION` (a frame before the first SECTION), `_BAD_SECTION {id}` (class above DEVICE, or a SECTION frame of the wrong size), `_SECTION_DUP {id}`, `_SECTION_FULL {max}`, `_SECTION_BIG {id, max}`, `_SECTION_EMPTY {id}`, `_ORDER` (a second program, or a section after it), `_CHUNKS {max}`, `_EMPTY`, `_NOT_STORED`, `_NOT_IN_STREAM`, `_RUN` (context NONE), `_BAD_ARG`. Map in `sys_project.h`.

## Open

- **Not run on a board.** It builds; `router_tests.py` has `test_project` (save, the IDs of the chunks, bad CRC / order, second save to the other bank, ERASE) and has not been run.
- Records a project no longer lists (a device record from an older project) stay stored; ERASE and a new save don't remove settings or device records.
- `CONFIG_SYS_PROJECT_MAX_BYTES` (64 KB) and the chunk count (32) bound the program; the 512 KB partition holds two banks plus the actions.
- The app side: the stream builder (sections in order, program last) and INFO's crc to skip an upload of the same project.
