# sys_project — the stored project (for agents)

Component `components/sys/sys_project/`. The pipeline that saves a whole **project** and runs it: the system settings, the devices' install and default commands, the user's actions and the **program** (VM loader frames, of any size). It sits on `sys_actions` (SYS_ACTIONS.MD): everything it stores is a record; it only decides which records, under which IDs, and in which order they run.

## Files

| File | Role |
|---|---|
| `sys_project.h` | API, ID macro `SYS_PROJECT_ID`, flags, error tags, the info and mark structs |
| `sys_project.c` | The stream parser, staging, commit, run, info, erase, boot |
| `sys_project_router.c/.h` | `project` router (`ROUTER_KEY_PROJECT` = 0x0C), op IDs `SYS_PROJECT_OP_*` |
| `contract_project.json` | Service contract definition (RPC commands and schemas) |
| `Kconfig` | Largest stream, sections, program chunks |
| `AGENTS.md` | Rules for changing this folder |

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

| Error | Severity | Context / Fields | Description |
|---|---|---|---|
| `ERR_PROJECT_BUSY` | MEDIUM | – | A save session is already open |
| `ERR_PROJECT_NO_SESSION` | MEDIUM | – | Write or commit without an active save session |
| `ERR_PROJECT_TOO_BIG` | MEDIUM | `u32 len, u32 max` | Stream length exceeds limit or announced size |
| `ERR_PROJECT_OFFSET` | MEDIUM | `u32 got, u32 want` | Offset mismatch (save continues; client resends) |
| `ERR_PROJECT_INCOMPLETE` | MEDIUM | `u32 got, u32 want` | Commit called before receiving all bytes |
| `ERR_PROJECT_CRC` | MEDIUM | `u32 got, u32 want` | CRC-32 mismatch |
| `ERR_PROJECT_BAD_FRAME` | MEDIUM | `u32 offset` | Frame length 0 or above frame limit |
| `ERR_PROJECT_TRUNCATED` | MEDIUM | – | Stream ends inside a frame |
| `ERR_PROJECT_NO_SECTION` | MEDIUM | – | Frame received before the first SECTION |
| `ERR_PROJECT_BAD_SECTION` | MEDIUM | `u32 id` | Class above DEVICE, or invalid SECTION frame size |
| `ERR_PROJECT_SECTION_DUP` | MEDIUM | `u32 id` | Duplicate section ID in stream |
| `ERR_PROJECT_SECTION_FULL` | MEDIUM | `u32 max` | Section count exceeds `CONFIG_SYS_PROJECT_MAX_SECTIONS` |
| `ERR_PROJECT_SECTION_BIG` | MEDIUM | `u32 id, u32 max` | Section exceeds class limit |
| `ERR_PROJECT_SECTION_EMPTY` | LOW | `u32 id` | Section contains no frames |
| `ERR_PROJECT_ORDER` | MEDIUM | – | Duplicate program section or section after program |
| `ERR_PROJECT_CHUNKS` | MEDIUM | `u32 max` | Program exceeds `CONFIG_SYS_PROJECT_MAX_CHUNKS` |
| `ERR_PROJECT_EMPTY` | LOW | – | Stream contains no sections |
| `ERR_PROJECT_NOT_STORED` | LOW | – | Run called when no project is stored |
| `ERR_PROJECT_NOT_IN_STREAM` | MEDIUM | – | SECTION op sent directly as a command |
| `ERR_PROJECT_RUN` | NONE | – | Context wrapper: project stored, but execution failed |
| `ERR_PROJECT_BAD_ARG` | HIGH | – | Null pointer or invalid argument |

## Limits

- `CONFIG_SYS_PROJECT_MAX_BYTES` (default 64 KB, up to 256 KB) bounds the total stream size.
- `CONFIG_SYS_PROJECT_MAX_SECTIONS` (default 40): maximum distinct settings, device and user sections in one project.
- `CONFIG_SYS_PROJECT_MAX_CHUNKS` (default 32): maximum program chunk records (`32 * CONFIG_SYS_ACTIONS_MAX_BYTES` = 128 KB max program size).
- Stored records that a new project omits (e.g. an older device configuration) remain in flash; ERASE and a new save do not remove existing settings or device records.
- One save session at a time (`ERR_PROJECT_BUSY`).

## Tests

`router_tests.py`, `test_project` (in `.claude/skills/runit-esp/scripts/`) covers:
- INFO when empty, RUN when empty (`ERR_PROJECT_NOT_STORED`).
- Input validation: frame before SECTION (`ERR_PROJECT_NO_SECTION`), section after program (`ERR_PROJECT_ORDER`), bad CRC (`ERR_PROJECT_CRC`).
- Verification that aborted or failed saves leave no staged records in flash.
- Complete project save with autostart and immediate run flags.
- Storing of sections under their IDs and chunked program records in sequence.
- Bank switching: second save alternating to the other bank and clearing old bank records.
- ERASE: removing the project record and both bank records.
