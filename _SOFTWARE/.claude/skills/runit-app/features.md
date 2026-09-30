# runIT App — Target Features (digest)

Source: the user's raw list [target-features.md](target-features.md) (kept verbatim). This file is the structured lookup: stable IDs, what each feature needs from firmware, gaps, decisions and tooling. **Nothing here is built yet.** Status of work lives in [../runit-root/PROGRESS.md](../runit-root/PROGRESS.md).

Status tags: **✅ decided** (survey 2026-09-24, §6) · **⚠ firmware gap** (§5).

---

## 1. Product principles (apply to every view)

| ID | Principle | Notes |
|---|---|---|
| P-1 | **Transport-agnostic connection** — BLE now, Wi-Fi and custom connectors later | Already the backend shape: `app/src/backend/stream/OutgoingRouter` routes a logical target (`runit.interface`) to bindings; BLE is one sender. A new connector = a new sender, no view changes |
| P-2 | **Two design levels: Basic and Advanced** — Basic hides or overrides elements | ✅ Same views in both modes; Basic shows fewer fields, safe defaults, automatic types. Must be data-driven: visibility / defaults per field should come from the descriptors, not hard-coded in views (⚠ F-GAP-1) |
| P-3 | **One responsive layout** for phone and desktop | ✅ Desktop web (Chrome/Edge, Web Bluetooth) first, then Android via Capacitor; iOS later. All views fully editable on the phone, touch-adapted: sidebars become drawers below a breakpoint |
| P-4 | **Everything generated from JSON** — settings, blocks, devices, contracts | Sources: `data-structures/**/*.generated.json` + `enums.json`, validated by `data-structures/schema/*.schema.json` |
| P-5 | **Runtime recreation** — blocks and objects rebuilt from what the device reports, for live telemetry | VM telemetry `0x04 0x42` describes heap objects the app didn't upload; `0x04 0x43` carries values |
| P-6 | **Everything interlinked** — any entity can jump to related entities (device ↔ contract ↔ block ↔ object ↔ remote widget ↔ action) | One relation index shared by navigation (§3 LNK) and error highlighting (§3 ERR) |
| P-7 | **Import / export everything** as JSON — code, settings, remote layouts, actions | One versioned project document (§4.2) |
| P-8 | **No user code** — the end user only selects options and places nodes | Expressions (EXPR) are edited as blocks/formula UI, never as text-only |
| P-9 | **Dark and light mode** | Tailwind `class` dark mode + CSS variables |
| P-10 | **Polish + English** | ✅ i18n keys from the start (no hard-coded UI strings) |

## 2. Shell and navigation

| ID | Feature | Notes |
|---|---|---|
| SH-1 | **Left sidebar** — explorer: category selector + view selector | Content changes with the view (palette on Code, widget palette on Remote, device list on Devices) |
| SH-2 | **Right sidebar** — collapsed: run / stop / step quick actions; expanded: inspector / settings of the selection | ✅ On the phone the collapsed rail becomes a bottom bar |
| SH-3 | **Auto-sized, collapsible side bars** | Remember sizes per view; auto-collapse on narrow screens |
| SH-4 | **Docs page + per-component info window** (how to use) | ✅ Later: only the info window (from descriptor `description` fields) at first; the docs page is postponed |
| SH-5 | Connection / device status always visible (link, run mode, errors badge) | Implied by debug + error features |

## 3. Views and functions

### SET — Settings page
| ID | Feature | Firmware link |
|---|---|---|
| SET-1 | BLE / connection setup | data connector settings (class `0x08`, `sys_data_connector_id_e`, `runit_data_provider_e` in `enums.json`) |
| SET-2 | Log setup (levels, what is streamed) | `sys_errors` log / severity, settings class `0x08` |
| SET-3 | App setup (mode Basic/Advanced, theme, layout) | app-only |
| SET-4 | All settings saved as JSON, exportable | ✅ Split: board settings (BLE, log, connectors) in the project JSON; app preferences (theme, mode, language, panel sizes) in a local, exportable app profile |
| SET-5 | Look depends on Basic/Advanced | P-2 |

### DEV — Devices and features page
| ID | Feature | Firmware link |
|---|---|---|
| DEV-1 | Install / test / set up devices | ✅ 2026-09-26 Board view (`app/src/devices/`): add from the device types, configure install values and pin groups, live install / uninstall; the stored code installs user devices at boot. Install decoders (class `0x01` `0x40–0x47`), `device_*.generated.json` |
| DEV-2 | Features (servo, H-bridge, …) with the devices they use | ⚠ F-GAP-2 features have no JSON descriptors yet |
| DEV-3 | Linked contracts per device, callable for testing | ✅ 2026-09-26 right panel Contracts tab: device ID filled from the device, answers decoded. `contracts.generated.json` (`response`, `response_stream`) |
| DEV-4 | **Board overview**: runIT board SVG mapping connected elements to physical terminals (STM32CubeMX-like) | Needs a board profile JSON (terminals ↔ device channels ↔ pins) ⚠ F-GAP-3 |
| DEV-5 | Hardware presence (what's actually there vs configured) | device sync / status contracts |

### ACT — Action setup (recorder)
| ID | Feature | Firmware link |
|---|---|---|
| ACT-1 | Compose an action: set the desired device states + add commands, then upload all under an action ID (✅ 2026-09-26 action composer: contract steps on any device, record / run) | `sys_actions` class `0x03`: record start `0x02 id` → frames → record stop `0x03`; 2 KB per action, IDs 1–255, NVS |
| ACT-2 | Action source stored in the project JSON (re-editable) (✅ 2026-09-26 `actions`) | app-side; device only keeps the raw blob |
| ACT-3 | **Sandbox mode** — build without sending live, with a readable summary of what the action does | ✅ While composing, device setup and commands are only buffered in the app, never sent. Upload must **store without executing** (nothing moves until the action is invoked) → ⚠ F-GAP-4 |
| ACT-4 | Size meter against the 2 KB limit (✅ 2026-09-26) | computed from packed frames |

### OBJ — Code: objects tab
| ID | Feature | Firmware link |
|---|---|---|
| OBJ-1 | User objects only (system objects hidden) | `vm-model.generated.json` object header, flags (`usr_protected`, …) |
| OBJ-2 | Create objects in a folder structure (PTR trees), assign values | `0x42` add objects, `0x43` set data |
| OBJ-3 | Basic = simplified view (auto type, hidden flags) | CMP-3 |
| OBJ-4 | Show linked blocks per object (jump) | LNK |
| OBJ-5 | Block outputs: (a) default, a hidden block-owned object per output / ENO (user sets only its name and subscription); (b) an output may target a user object instead | `0x45` `out_obj_ids` / `eno_obj_id`, `ERR_VM_BLK_OUTPUT_TAKEN` |

### CAN — Code: canvas
| ID | Feature | Notes |
|---|---|---|
| CAN-1 | Block palette + object palette (tree) on the left, drag & drop onto canvas | blocks from `vm/blocks/*.generated.json`. ✅ blocks 2026-09-27 (drag or click); ✅ variables onto pins 2026-09-28 (§8.1 chips) |
| CAN-2 | Feature list (created features) as a palette source | DEV-2 |
| CAN-3 | Block settings in the inspector when a block is selected | pins, `custom` fields from descriptor `state` layout (source `user`). ✅ 2026-09-27 (`BlockDetails`, formula editor for EXPR / EXPR_BIT) |
| CAN-4 | **Multiple canvases**, executed in order (canvas 1 first) | ✅ Node-RED-style flow tabs sharing the same objects; one program, tabs concatenated in tab order within a pass. A tab can be disabled: the compiler leaves it out (re-upload needed, no live toggle) |
| CAN-5 | Two block looks: basic and extended | ✅ 2026-09-27: toolbar toggle (saved preference), inspector can fix each block to simple/detailed (saved in project); basic = type name + input/output pins, extended = ID, formula or descriptor user settings, pin paths and activation. Red EN / green ENO side strips always visible; referenced ENO storage is allocated automatically; live values / runtime state await DBG |
| CAN-6 | Snap-to-grid | ✅ 2026-09-27: 20-unit grid, toggle in the action bar; blocks will snap their top-left corner |
| CAN-7 | Loops visibly marked (FOR span) | ✅ 2026-09-28: FOR body = the blocks its Body handle gates; tinted areas for loops and branches (§8.1) |
| CAN-8 | Live values on blocks and wires while running | DBG |

### CMP — Compiler (app-side, pure TS)
| ID | Feature | Firmware link |
|---|---|---|
| CMP-1 | Compile project → VM packets `0x40…0x48` in the required order (✅ 2026-09-27: `0x41`–`0x45`, `0x47`, `0x48`; board-tested) | `vm-program.generated.json` (records, widths, limits, `total_size` formulas) |
| CMP-2 | **Bad-pattern detection** (lint) before upload | see §4.3 list |
| CMP-3 | Basic mode: **automatic object types** from the pins they connect to | pin `@value` kinds in block descriptors |
| CMP-4 | Memory / limit budget (arena size, Kconfig limits, frame size) shown before upload | Kconfig limits + arena formulas in `vm-program.generated.json` |
| CMP-5 | Source map: every packet / block index ↔ canvas node | needed by ERR and DBG |

### DBG — Debug view
| ID | Feature | Firmware link |
|---|---|---|
| DBG-1 | Run / stop / pause | `0x48` exec commands (`vm_exec_command_e`) |
| DBG-2 | Step loop by loop (one pass) | `VM_RUN_STEP` exists |
| DBG-3 | Step block by block | `VM_RUN_BLOCK_STEP`, `next_block` exists — ⚠ F-GAP-5 confirm it's reported to the app |
| DBG-4 | Live data from telemetry on objects and blocks | `0x47` subscribe, `0x02` telemetry stream |
| DBG-5 | **Flow state** — active path (which blocks ran / ENO) and block results (output values) | ✅ Both possible today: ENO is an object (`eno_obj_id`) and outputs are objects → the compiler allocates them and debug subscribes. Block internal state (timer elapsed, latch) is not required |
| DBG-6 | Dry run: logic runs, outputs don't move | firmware "freeze mode" gives this already (VM_EXEC.MD) |

### REM — Remote control view
| ID | Feature | Notes |
|---|---|---|
| REM-1 | Separate view: widget palette (sliders, switches, displays, …) left, configuration right | SH-1 / SH-2 |
| REM-2 | Widgets linked to objects | write via `0x43` live patch (data-only objects while running) |
| REM-3 | "Overridden after change" | ✅ The widget follows the live object value; when the user moves it, the app writes the object; the program may change it again afterwards (no forcing) |
| REM-4 | Physical gamepad input | ✅ In scope: Gamepad API, bind sticks / buttons to widgets or objects |
| REM-5 | Emergency stop always visible (legacy idea) | static action freeze / VM stop |

### CON — Console (Advanced)
| ID | Feature | Firmware link |
|---|---|---|
| CON-1 | Read-only console with decoded data stream | all streams (`0x02` telemetry, `0x05` responses, logs) decoded via the JSON descriptors |
| CON-2 | **Command ↔ ack mapping** per call (e.g. one contract call → its response) | `[seq]` byte, response `[0x05][seq][class][packet][status][data]`; PROGRESS "Command responses" |

### LNK — Relations and navigation
| ID | Feature |
|---|---|
| LNK-1 | Relation index over the project: device ↔ contract ↔ feature ↔ block ↔ object ↔ widget ↔ action |
| LNK-2 | "Show linked" on any entity + jump with back/forward history (Device → blocks → objects → blocks …) |
| LNK-3 | Same index drives highlight (selection, errors) |

### ERR — Errors
| ID | Feature | Firmware link |
|---|---|---|
| ERR-1 | Show errors reported by runit-esp, decoded | `u8 len · u16 tag · u16 owner · payload`, stable tag IDs, schema ID (SYS_ERRORS.MD) |
| ERR-2 | Link error → entity: device (dev_id in tag payload), pin, block (`block_idx` / `blk_id` in VM tags) | ⚠ F-GAP-7 tag payload layouts must be published as JSON |
| ERR-3 | Interlinked highlight: light up the device, the block and the objects involved | LNK-3 + CMP-5 |
| ERR-4 | Command failures (response status + `u16 tag, u16 owner`) attached to the command in CON-2 | |

### IO — Project files
| ID | Feature |
|---|---|
| IO-1 | Load code and all settings from external JSON |
| IO-2 | Export code to external JSON |
| IO-3 | Versioned format + migrations; firmware compatibility check (schema ID / descriptor hashes) on connect |

---

## 4. Architecture (stack accepted 2026-09-24; layers are a proposal)

### 4.1 Layers
```
views (React)            Settings · Devices · Actions · Code(objects/canvas) · Debug · Remote · Console · Docs
  └─ app state           project store (document) · session store (connection, run mode) · live store (telemetry)
       └─ domain (pure TS, no React, unit-tested)
            descriptors  load + validate generated JSON → typed catalogs (devices, contracts, blocks, settings, enums)
            compiler     project → VM / install / action packets, lint, budget, source map
            decoder      frames → typed events (telemetry, responses, errors, logs)
            relations    relation index for navigation + highlight
            └─ backend   packetPack · stream router · transports (BLE now; Wi-Fi/Serial later)
```
- Telemetry must **not** go through React state per frame: a live store with per-object subscriptions (`useSyncExternalStore`) so only the widgets showing that object re-render.
- Compiler, decoder and relations are pure functions → testable with golden vectors already in the descriptors (EXPR opcodes, packet layouts).

### 4.2 Project document (one JSON)
`{ format_version, firmware: {descriptor hashes}, settings, board, devices[], features[], objects (tree), canvases[] (ordered), remote_layouts[], actions[], ui (positions, collapsed state) }`. Entities keyed by stable IDs so relations and error mapping survive edits. App preferences (theme, mode, language, panel sizes) live in a separate app profile (SET-4).

### 4.3 Bad-pattern checks (CMP-2), first list
- required pin unwired; type/value-kind mismatch across a link
- two blocks writing the same object or actuator (firmware: last writer wins — VM_EXEC.MD asks the editor to flag it)
- read before first write in the same pass (order-dependent logic across canvases)
- FOR loop without bound / nested span depth over limit (`ERR_VM_EXEC_SPAN_DEPTH`)
- EXPR stack under/overflow (opcode table pops/pushes)
- arena / object / block counts over Kconfig limits; frame bigger than link MTU
- object name > 15 chars, duplicate names in a folder
- action blob > 2 KB; action writing to a protected / onboard device in Basic mode
- descriptor `//@rule` load rules checked in the app first (same error tags as firmware)

### 4.4 Ideas beyond the list
- **Upload diff**: before upload, show what changes on the device (objects added, blocks changed, retained values lost).
- ✅ **Program on device ↔ project**: the project file is the source of truth; the board stores the built code (`sys_project`, app/docs/03_records_as_storage.md) and reports its CRC, so the app knows whether the board holds the open project; reading it back is recovery only.
- **Timeline / recorder** (legacy doc 06): record telemetry + commands + errors, scrub back in Debug.
- **Wire value probes**: pin a value on any link; watch list in the right sidebar.
- **Templates**: starter projects (blink, servo sweep, motor ramp) — good for Basic users.
- **Web Serial transport** (USB cable) as a second connector — cheap to add, useful when BLE fails.
- **Undo / redo** across the whole project document.

## 5. Firmware gaps found by this digest

| ID | Gap | Needed for |
|---|---|---|
| F-GAP-1 | No per-field `basic` / `advanced` visibility or default in the annotation generators | P-2, SET-5, DEV |
| F-GAP-2 | Features (servo, hbridge) have no generated JSON descriptors | DEV-2, CAN-2 |
| F-GAP-3 | No board profile JSON (terminals, connectors, which device channel sits where). Started 2026-09-25: `data-structures/board/board.generated.json` has the static device IDs, names and descriptors; terminals and channel wiring still missing | DEV-4 |
| F-GAP-4 | Action recording executes the frames; upload must store without executing (record-only flag or direct blob upload) | ACT-3 |
| F-GAP-5 | Check that pause / block-step state (`next_block`, run mode) is reported to the app | DBG-3 |
| F-GAP-7 | ✅ 2026-09-25: error tags, owners, severities, payload layouts, message templates and value tables published (`data-structures/errors/errors.generated.json`, `generate-errors.py`). Left: 6 tags whose `LOG_BODY` uses conditionals have no template; `esp_err_to_name` / `vm_format_obj_id` values print as numbers | ERR-2 |
| F-GAP-8 | Device JSON doesn't list the events each device publishes (already in PROGRESS) | event subscriptions, LNK |
| F-GAP-9 | ✅ 2026-09-26: stored code CRC (`info`) and the CRC of the code the boot replayed | §4.4 |
| F-GAP-10 | `"required": false` (`//@optional`) in contracts / settings JSON means "may be 0 / the sentinel", **not** "may be left off the wire": decoders need the whole packed struct (a shorter frame gets `ERR_INTERFACE_SHORT_FRAME`, seen on the devkit 2026-09-24). The schema doesn't say so | Packer in the app: always send every field. Either document it in the schemas or rename the flag |

These stay here only (not in PROGRESS.md) until picked up — user decision 2026-09-24.

## 6. Decisions (survey 2026-09-24)

| Topic | Decision |
|---|---|
| Platforms | Desktop web (Chrome/Edge) → Android (Capacitor); iOS later |
| Basic mode | Same views, fewer fields; visibility from descriptors |
| Phone | All views editable, touch-adapted; right rail → bottom bar |
| Canvases | Node-RED-style flow tabs, run in tab order in one program, per-tab disable at compile time |
| Debug flow state | Active path / ENO + block results; no block internal state |
| Remote sync | Widget follows device value; user edit writes the object; no forcing |
| Actions | Composed in an app-side buffer, nothing sent; upload stores without executing (F-GAP-4) |
| Readback | None; project file is the source; device keeps a project hash |
| Settings | App profile (theme, mode, language, panels) separate from project (board settings) |
| Stack | §7 accepted, incl. Tailwind v4 |
| Existing `App.tsx` | The app shell (`main.tsx` renders it) |
| Extras in scope | Gamepad in Remote; Polish + English |
| Out of scope for now | Offline simulated board; several boards at once; docs page (info window only) |
| Firmware gaps | Tracked in §5 only, not in PROGRESS.md |
| Block outputs (OBJ-5, 2026-09-25) | (a) is the default. (b) is allowed: the object becomes block-driven. Compiler rules: one writer per object (the device also refuses a second one, `ERR_VM_BLK_OUTPUT_TAKEN`); `mutable` forced on; `upd_resetable` = 1 for driven objects, 0 for other user objects (derived at compile time, not stored). Editor: a driven object is read-only (no remote write widget, no second output, no write-target block), shows a lock and "driven by Block N" with a jump; the pin shows `→ folder/name`. Reading is unrestricted: wires from the pin compile to an accessor on the object either way |

New open questions go below this table.

## 7. Tooling (✅ accepted 2026-09-24)

| Need | Suggestion | Why / alternative |
|---|---|---|
| Canvas | **React Flow (`@xyflow/react`)** | Custom nodes, snap grid, minimap, touch; legacy design already chose it. Alt: Rete.js |
| Auto-layout | `elkjs` | Tidy imported / read-back programs |
| State | **Zustand** (+ `immer`, `zundo` for undo/redo) | Small, slice-based, works outside React (compiler, decoder). Alt: Redux Toolkit |
| Live telemetry | own store + `useSyncExternalStore` | Per-object re-render only |
| UI primitives | **Radix UI** via **shadcn/ui** (Tailwind) | Accessible menus, dialogs, tabs, tooltips; copied into repo, no lock-in |
| Resizable / collapsible panels | `react-resizable-panels` | SH-3 |
| Drag & drop (outside canvas: trees, widget grid) | `dnd-kit` | Touch support. Remote grid alt: `react-grid-layout` |
| Charts / live plots | `uPlot` | Fast time series for telemetry. Alt: Recharts for simple gauges |
| Routing / views + deep links | TanStack Router | Typed routes, back/forward for LNK-2 |
| JSON validation | `ajv` against `data-structures/schema` | Same schemas as the generators |
| Types from schemas | `json-schema-to-typescript` | Typed descriptor catalogs |
| Local storage of projects | IndexedDB via `idb-keyval` or Dexie | Autosave, recent projects |
| Docs rendering | `react-markdown` (+ MDX later) | SH-4 |
| Tests | **Vitest** | Compiler / decoder golden vectors from the descriptors |
| Mobile | Capacitor + `@capacitor-community/bluetooth-le` | Native BLE on Android/iOS behind the existing BLE adapter interface |
| Wi-Fi transport | WebSocket on the ESP | Browsers can't open raw TCP/UDP |
| Styling | Tailwind v4 (repo has 3.4) | Upgrade before building the shell |
| i18n | `i18next` + `react-i18next` | P-10 |
| Gamepad | browser Gamepad API (no library) | REM-4 |

## 8. Wiring and enables (proposed 2026-09-27; decided 2026-09-28, §8.1 wins where they differ)

Firmware semantics this rests on: `components/VM/VM_EXEC.MD` §1 and `vm_block_is_enabled()` (`core/block/vm_block.h`).

**Storage: no format change.** A wire lives on the input side: `inputs[i]` is an `ObjectPath`.
- output → input: `{ root: "<block>:q<n>" }` (fan-out free, one source per input);
- variable → input: the variable's path (`count`, `motor.gains[2]`, `table[sel]`);
- output → variable: `outputs[i] = "<object id>"` (one writer per object, `ERR_VM_BLK_OUTPUT_TAKEN`);
- enable: `enables[]` paths; wiring from `<block>:eno` turns `eno` on for that block.

**Three kinds of connection** (VM_EXEC.MD): data wire = order + value; EN wire = order + gating; variable label = neither (a global read).

**Gestures (proposal):** drag output → input (or back), candidate pins light up, dropping on a wired input replaces it; tap-tap on touch; click a wire + Delete. Variables are dragged from the Variables tab onto a pin and shown as a **label on the pin** (not a canvas node), which also carries wires across canvases. Nested / dynamic sources get a per-pin source editor in the details panel.

**EN / ENO on every block** (IF included: nesting is IF₂ enabled by IF₁ yes; a disabled IF drives both outputs false, so the whole branch below stops):

| Activation | Blocks | EN | Unconnected EN |
|---|---|---|---|
| every pass | IF, SWITCH, FOR, PERIODIC, TIMER, IO_SET_LEVEL, ON_EVENT | active while; disabled: gates false, TIMER resets, PERIODIC disarms, IO_SET_LEVEL does its `disabled_action` | runs every pass |
| EN edge | EDGE | samples the combined EN level every pass, including false; pulses ENO on the selected rising, falling or either edge | invalid: connect at least one EN source |
| held flow | LATCH | samples EN as Set every pass; optional Reset is read independently, and ENO holds the state until Reset | invalid: connect at least one EN source |
| triggered | EXPR, EXPR_BIT, SET, CLONE | extra condition: fresh input **and** enabled | runs on every fresh input |
| enable-rising | ACTION, IO_TOGGLE | the trigger itself (label it "Do") | **fires once at program start** (warn) |

- 0 sources = always enabled, except EDGE and LATCH, which require an EN source; a wired source that can't be read = off (fail closed).
- Mode **any** (default, branches rejoining) / **all** (independent conditions), one per block, shown as ∨ / ∧ on the EN pin from the second wire; nested logic goes through an EXPR / logic block into one EN.
- ENO: each block defines its flow signal. Ordinary action blocks publish true when they act; EDGE pulses for one pass; TIMER and LATCH publish held levels. True is loud, false clears quietly.

**Execution order (proposal):** sorted from the wires per canvas (a block runs after what it reads), position left→right, top→bottom as the tie-break, the order shown on each block; a cycle reads the previous pass (the closing wire marked); FOR's body becomes a frame on the canvas instead of the `body` count. Canvases run in tab order.

**Decisions asked of the owner:** (1) variables as pin labels (recommended) or canvas nodes; (2) automatic order from wires (recommended) or manual; (3) EN + ENO on every block (recommended). **Firmware (G-11):** triggered blocks never run on an enable alone; proposed fix: a fresh enable source counts as a trigger.

### 8.1 Decided 2026-09-28 (owner's notes `wire.md` + answers); built the same day

Built: `domain/canvas/arrange.ts` (order, explicit gates, loop bodies), `wiring.ts`, `variablePath.ts`; canvas wires, pin picker, chips, block details, areas (runit-app SKILL.md code map). Firmware G-11 fixed, not yet tested on a board.

**Explicit enables and order (owner update, 2026-09-28).** A block with no EN source is always enabled; only sources wired directly to EN control it. Data wires set execution order but do not pass gates from one block to another. Execution: per canvas, the trees (blocks joined by wires) top to bottom, inside a tree after what feeds it, then the next canvas.

**Explicit gates only (owner update, 2026-09-28).**
- A *gate* is an output of kind `gate` (IF yes/no, SWITCH branch), a PERIODIC or EDGE ENO pulse, or any bool output the user drops on a block's orange EN strip. A block is gated only by paths explicitly wired to its EN; inherited feeder gates and the Branch control are removed.
- Several explicit EN sources combine as any (∨) by default or all (∧) when selected on the block. Nested logic goes through an EXPR / logic block wired to EN.
- A disabled IF drives its yes and no outputs false; a block that reads either output still needs its own EN wire if it should follow that branch.
- ENO (green strip) is opt-in: dragged onto another block's EN it gates that block on the source's flow signal (action success, pulse, or held level, depending on the source block). Nothing else is linked automatically.
- LATCH uses its EN as Set, its optional Reset data input to clear, and its ENO as the held flow level. It has no Set or Q data pin. A one-pass pulse on EN can keep a downstream chain enabled until Reset, even while LATCH's EN is false.
- The canvas shows each explicitly gated branch as a rounded area with a plain transparent tint around the blocks carrying that gate; areas may overlap. An area holding another grows to wrap it and the inner one is drawn on top. The same for IF, SWITCH, PERIODIC … and FOR.
- EN links are drawn as a faded ribbon, narrow at the source and as tall as the whole red EN strip where it arrives (gate red, ENO green, a FOR's loop link purple); data wires stay thin lines.

**FOR body = the FOR's ENO chain** (owner, 2026-09-28, replacing a Body output). A FOR's green ENO dropped on a block's EN puts that block in the loop, and with it every block whose chain depends on it (data wires, or gated by a member). The link is compiler-only, never a VM enable; the compiler places the body right after the FOR and sets the span. Older `<for>:body` links still load.

**Firmware G-11: a fresh enable counts as a trigger.** Triggered blocks (EXPR, EXPR_BIT, SET, CLONE) also activate when an enable source is true and freshly written; gates and ticks are written loud while true.

**Accessors (variables on the canvas).**
- A short rectangle ("chip") named after the accessed object, truncated from the front (`…temperature`); click → the right panel edits the path when it needs more than a drag (index, dynamic parts).
- Dragged from the Objects tree (folders and variables) onto a block: while dragging over a block a zoomed view shows its pins and EN; a pin accepts it on drop or by click. Only compatible pins accept: number ⇄ bool cast; `object` / `ptr-cell` pins and strings only take their own kind.
- An empty accessor from the toolbar drops on any pin; the compiler still checks the final path.
- The chip snaps onto the pin (no wire); pulled away it keeps a wire; one detached chip may feed several pins.
- On an **output** pin the chip means the block writes that variable (one writer per object).

**Strips:** EN orange, ENO green (as built).
