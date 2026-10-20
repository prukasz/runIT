# sys_project — the stored project (for agents)

Component `components/sys/sys_project/`. The pipeline that saves a whole **project** and runs it: the system settings, the devices' install and default commands, the user's actions and the **program** (VM loader frames, of any size). It sits on `sys_actions` (SYS_ACTIONS.MD): everything it stores is a record; it only decides which records, under which IDs, and in which order they run.

## Files

| File | Role |
|---|---|
| `sys_project.h` | API, ID macro `SYS_PROJECT_ID`, the section IDs, the boot option `sys_project_boot_e`, error tags, the info and mark structs |
| `sys_project.c` | The stream parser, staging, commit, run, info, erase, boot |
| `sys_project_router.c/.h` | `project` router (`ROUTER_KEY_PROJECT` = 0x0C), op IDs `SYS_PROJECT_OP_*` |
| `contract_project.json` | Service contract definition (RPC commands and schemas) |
| `Kconfig` | Largest stream, sections, program chunks |
| `AGENTS.md` | Rules for changing this folder |

## The save: one stream

The app sends the project as **one stream**: frames back to back, `[u16 len LE][key][dir][…]` (the image format of a record), cut into **sections**. A section opens with a SECTION frame `[0x0C][0x01][0x10][u32 id]`; the frames after it belong to it.

| Section id | The frames become |
|---|---|
| `!= 0` (class USER / SYSTEM / DEVICE, e.g. `0x01000001` log, `0x02000063` device 99) | the record under that ID, as they are, at COMMIT |
| `0` (`SYS_PROJECT_SECTION_PROGRAM`) | the program: cut into chunk records of whole frames (`CONFIG_SYS_ACTIONS_MAX_BYTES` at most), **numbered one after another by the pipeline** |
| `0xFFFFFFFF` (`SYS_PROJECT_SECTION_START`) | the **start**: what starts the loaded project (the app's EXEC normal), one record in the bank (`n = 0xFFFF`), not in the project record: RUN runs it, LOAD leaves it out. The firmware does not know the VM here |

The order is the run order: the records (system, devices), then the program, then the start; one program and one start at the most, either may be missing (`ERR_PROJECT_ORDER`). Sections are stored in the stream's order, each at most the limit of its class (a device record 512 bytes).

```
stream:  [SECTION 0x01000001][log CONFIG write]                    -> record 0x01000001
         [SECTION 0x02000000][CREATE, defaults ...]                -> record 0x02000000
         [SECTION 0][frame][frame]...(any number)                  -> chunks 1, 2, 3 ... and the list 0
         [SECTION 0xFFFFFFFF][EXEC normal]                         -> the start, 0xFFFF
```

### IDs one after another

Class `0x03` PROJECT (`SYS_ACTIONS_CLASS_PROJECT`), index `[bank+1 u8][n u16]` (`SYS_PROJECT_ID(bank, n)`). Two **banks** alternate: a save fills the one the stored project doesn't use, so the stored project's records are never touched. In a bank `n = 0` is the program's **list of chunks** (a record of `RUN <chunk>` frames, stops at a failing chunk), `n = 1, 2, 3 …` are given in the order they are needed: a program chunk, or a section waiting for COMMIT; `n = 0xFFFF` (`SYS_PROJECT_N_START`) is the start section's record. `0x03000000` (`SYS_ACTIONS_ID_PROJECT`) is the **project record**.

### Commit = one store

Nothing a project changes is touched while the stream comes: every section is staged in the spare bank. COMMIT checks the length, the CRC (zlib) and the framing (a cut inside a frame is `ERR_PROJECT_TRUNCATED`), then:

1. stores the program's list of chunks;
2. copies every staged section to its own ID (`sys_actions_copy`, no flash write when it is the same image) and frees the staging record;
3. stores the **project record**: `[0x0C][0x01][0x11 MARK][u8 bank, u32 stream crc32, u32 stream length]`, then one `RUN_ALL <id>` frame per section in order, then `RUN <list>` for the program. **This is the commit point**: until it is stored, the old project is the stored one;
4. clears the other bank (the old program).

A failed or aborted save (`ABORT`, a write error, a bad CRC, the next `BEGIN` after a power cut) drops the staged records and leaves the stored project and the settings as they were. Only a flash failure *inside* step 2 can leave some settings new: they are separate records.

**Load and run.** LOAD (`sys_project_run(false)`) = `sys_actions_run(project record, keep_going = true)`: the settings and devices run with `RUN_ALL` frames (one failing device leaves the rest configured), the program with `RUN` (stops at its first failing frame; the VM loader fails closed anyway). The program is then loaded and stopped: it waits for the user (the app's Play). RUN (`sys_project_run(true)`) loads it and then, when all of it loaded, runs the start section (none: nothing more). Depth: project record → list → chunk = 3 RUNs, so `CONFIG_SYS_ACTIONS_MAX_DEPTH` must be at least 3 (a `_Static_assert`).

**At start: the boot option** (`sys_project_boot_e`, a u8 in the `project` namespace of the actions partition, key `boot`; `SYS_PROJECT_BOOT_NONE` when never set): `NONE` nothing, `LOAD` load, `RUN` load and start. BEGIN carries it; COMMIT stores it and **does it right after the save**, as a boot would (a failure there is `ERR_PROJECT_RUN` wrapping the cause: the project **is stored** anyway). OPTIONS reads and sets it alone (`ERR_PROJECT_BAD_BOOT` for another value).

**A new project replaces the old one whole**: VM RESET, ERASE (the old project's devices go, the board's stay), then the save. Its run creates the devices again from their CREATE frames, so a project runs onto a board without devices of its own: after a boot or an ERASE. A second RUN of the same stored project without either finds its devices taken (`ERR_DEV_ID_TAKEN`; `RUN_ALL` goes on, the devices keep what they had).

**Every section runs at every start.** The project record runs each section in order, whatever its class: an action stored as a section (a user ID) is run at the start like the settings and devices around it. A user action that must wait for a call (a button, the VM's ACTION block, an error policy) is stored with the actions router (BEGIN / WRITE / COMMIT, SYS_ACTIONS.MD), not as a section. IDs 3 to 6 are the firmware's own actions (RUNIT.MD "Onboard actions") and are not for the app.

**Boot.** Last boot step `project at start` (`sys_project_boot`): with a stored project it queues LOAD `[0x0C][0x01][0x12]` or RUN `[0x0C][0x01][0x06]`, as the boot option says, with `router_inject` (on a router task, after the devices are created). With `NONE` nothing replays by itself, as everywhere else.

## Remote (`sys_project_router`, `ROUTER_KEY_PROJECT` = 0x0C)

| Op | Request | Response |
|---|---|---|
| `0x01` BEGIN | `u32 len, u8 boot` (`sys_project_boot_e`: stored and done at COMMIT) | – |
| `0x02` WRITE | `u32 offset`, data | – (offset = bytes so far; a wrong one is `ERR_PROJECT_OFFSET` and the save goes on, any parse error drops it) |
| `0x03` COMMIT | `u32 crc32` | – |
| `0x04` ABORT | – | – |
| `0x05` INFO | – | `u8 stored, u8 boot, u8 bank, u32 crc32, u32 bytes` (crc and length of the stream it was saved from: the app compares them with its project) |
| `0x06` RUN | – | – (load, then the start section; `ERR_PROJECT_NOT_STORED` with none) |
| `0x07` ERASE | – | – (the devices the project created are removed, `sys_device_sweep(REMOVE)`: the board's stay; then the project record and both banks; the settings, device and user records stay). Stop the VM first (VM RESET): its program would call devices that are gone |
| `0x08` OPTIONS | write: `u8 boot`; read: – | write: –; read: `u8 boot` |
| `0x10` SECTION | `u32 id` | only a frame of the stream: sent as a command it is `ERR_PROJECT_NOT_IN_STREAM` |
| `0x11` MARK | `u8 bank, u32 crc32, u32 bytes` | first frame of the project record; does nothing when run |
| `0x12` LOAD | – | – (load without the start section: the program stays stopped; `ERR_PROJECT_NOT_STORED` with none) |

One save at a time (`ERR_PROJECT_BUSY`). Reading a project back is the actions router's READ: the project record (`MARK`, then one `RUN_ALL <section id>` per section and `RUN <list>` for the program), each section's record, the list, the chunks it names and the start record (`SYS_PROJECT_ID(INFO's bank, 0xFFFF)`; not found: none); the program's frames come out exactly as they went in, so the CRC of the stream rebuilt from them equals the `INFO` one. The app reads them in `runit-app/src/backend/session/project.ts` (`readProject`) and decompiles the program into a project (`Engine.recover`).

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
| `ERR_PROJECT_ORDER` | MEDIUM | – | A second program or start, a section after the program, anything after the start |
| `ERR_PROJECT_CHUNKS` | MEDIUM | `u32 max` | Program exceeds `CONFIG_SYS_PROJECT_MAX_CHUNKS` |
| `ERR_PROJECT_EMPTY` | LOW | – | Stream contains no sections |
| `ERR_PROJECT_NOT_STORED` | LOW | – | Run called when no project is stored |
| `ERR_PROJECT_NOT_IN_STREAM` | MEDIUM | – | SECTION op sent directly as a command |
| `ERR_PROJECT_RUN` | NONE | – | Context wrapper: project stored, but execution failed |
| `ERR_PROJECT_BAD_ARG` | HIGH | – | Null pointer or invalid argument |
| `ERR_PROJECT_BAD_BOOT` | MEDIUM | `u8 value` | BEGIN or OPTIONS with a value that is not a `sys_project_boot_e` |

## Limits

- `CONFIG_SYS_PROJECT_MAX_BYTES` (default 64 KB, up to 256 KB) bounds the total stream size.
- `CONFIG_SYS_PROJECT_MAX_SECTIONS` (default 40): maximum distinct settings, device and user sections in one project.
- `CONFIG_SYS_PROJECT_MAX_CHUNKS` (default 32): maximum program chunk records (`32 * CONFIG_SYS_ACTIONS_MAX_BYTES` = 128 KB max program size).
- Stored records that a new project omits (e.g. an older device configuration) remain in flash: ERASE and a save do not remove settings or device records. The app removes the ones its project no longer has (`writeToBoard`).
- One save session at a time (`ERR_PROJECT_BUSY`).

## Tests

`runit-app/tests/hardware/f-project.hw.test.ts` (over BLE, on the PCB: a record stored, read back, run; the project saved, INFO, every record read back, the program recovered, LOAD leaves the VM stopped, RUN starts it, boot RUN across a restart, erase) and `router_tests.py`, `test_project` (over the console, in `.claude/skills/runit-esp/scripts/`) covers:
- INFO when empty, RUN when empty (`ERR_PROJECT_NOT_STORED`).
- Input validation: frame before SECTION (`ERR_PROJECT_NO_SECTION`), a section after the program and the program after the start (`ERR_PROJECT_ORDER`), boot 3 (`ERR_PROJECT_BAD_BOOT`), bad CRC (`ERR_PROJECT_CRC`).
- Verification that aborted or failed saves leave no staged records in flash.
- Complete project save with boot RUN (done right after the commit), then RUN and LOAD.
- Storing of sections under their IDs and chunked program records in sequence.
- Bank switching: a second save (with a start section, boot NONE) in the other bank, the old bank's records cleared, the start record at `n = 0xFFFF`, RUN with it.
- ERASE: removing the project's devices, the project record and both bank records.
