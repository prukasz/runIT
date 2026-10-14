# The VM engine (for agents)

`runit-app/src/engine/`: everything an app needs to build on the runIT VM, with no UI, no DOM and no connection: the block palette, the user's variables and paths (accessors), block wiring, execution order, and the compiler that turns a project into the frames the board loads. Written from the firmware (`components/vm/`) and `generated/`; the old app (`legacy/`) was only read for ideas.

```ts
const engine = createEngine()                       // bundled descriptors; new Engine({definitions, limits, codecs}) for your own
const result = engine.compile(project)              // { ok, diagnostics, frames, ir, arena, ids, order }
if (result.ok) for (const f of result.frames) send(f.op, f.payload)   // OPEN, objects, values, accessors, blocks
```

A `Project` is `{ variables, blocks }`, plain data. The engine holds neither a project nor a connection: data in, data out.

## Layout (dependencies point down)

| Module | What it is |
|---|---|
| `wire/` | The wire format: object / accessor / block records (`records.ts`), batching into frames (`frames.ts`), the arena the board will use (`arena.ts`), what the board refuses at load (`check.ts`), limits and the frame arithmetic (`limits.ts`). Numbers firmware publishes arrive as a `WireDefinitions`. |
| `generated/` | The one place that reads `generated/`: the wire numbers (`wire.ts`), the board's limits (`limits.ts`) and the descriptor files (`index.ts`, Vite globs). Everything else takes parsed data as arguments. |
| `catalog/` | The block palette. `fromGenerated.ts` is the only file that knows the descriptor format (`vm-block` v3): it resolves enums, and a device variant's contract / device command into the variant's fields (the arguments), pins and outputs. `shape.ts` gives the pin shape the board checks (`shapeOf`). `layouts.ts`: where the values sit in a device block's state (the descriptor does not describe the struct). |
| `model/` | The project: `variables.ts` (tree of values, folders, references; edits return a new tree), `paths.ts` (accessors as the user sees them; parse, format, resolve), `layout.ts` (variables as objects: ids, flags, initial data), `program.ts` (`BlockInstance`, `Source`), `pins.ts` (groups expanded), `diagnostics.ts`. |
| `graph/` | `arrange.ts`: execution order (wiring, then the user's order), gates, feedback, loop bodies made contiguous with their spans. |
| `compiler/` | `compile.ts` (the pipeline), `accessors.ts` (paths to accessors), `state.ts` + `values.ts` (a block's state bytes), `codecs.ts` (extension point), `expression.ts` (formulas: parser, RPN, the two expression codecs). |

## How a project becomes frames

1. Variables are checked and laid out as objects (ids `0..n-1`, depth-first). A folder is a pointer object over its children, a reference one over its target; an empty folder has no object.
2. Blocks are arranged: a block runs after what it reads (outputs, enable-outs, variables a block writes); a loop block is followed directly by its body (everything gated by its enable-out and what depends on it) and carries `[start, end)` in its state.
3. Each block gets the objects it writes: the variable the user named for an output, else an object of its own (bool for flow, i32 for int, f32 for float; `usr_protected`), and one for its enable-out when something reads it.
4. Every reading pin becomes an accessor. A path is **folded into its root** where the walk is the same every time (`motors[1][2]` is root = the object of `motors[1]`, step `[2]`: the board's fast path), but not through a folder a `cell` pin names (clone re-links it) nor past a run-time position. Identical accessors are made once; an accessor a path reads is made before it (the board needs it loaded).
5. State is encoded (its length is the descriptor's `state_len`): a core block's fields at their offsets, a loop's span, a formula by its codec; a device block as `vm_block_device_data_t` (56 bytes): device id, op, idle op, `k[]` constants (the bits of a float for a scaled argument). `op` is the u16 the firmware builds: `(contract_id << 8) | n` for a contract command, `0xFF00 | n` for a device's own command, never the bare `op` of the descriptor.
6. The program is held against `wire/check.ts` (the loader's rules), then framed to fit the board's frame (`wire/limits.ts`); a record that cannot fit one is an error (`ERR_ROUTER_FRAME_LEN`). **A mistake in a project is a diagnostic, never a thrown error**; a program with any error has no frames.

## Where each fact comes from

| Fact | Source | Held against it by |
|---|---|---|
| Op ids, enum values, flags | `generated/common/enums.json`, `generated/services/contract_vm.json` | `wire/frames.test.ts` (against the firmware's own numbers) |
| Record layouts, arena rules, load checks | `components/vm/vm_core/loader/vm_wire.h`, `store/vm_store.c`, `block/vm_block_build.c`, `loader/vm_loader.c` | `vm-golden.json` frames byte for byte; the arena test replays the allocator |
| Blocks: pins, fields, offsets, variants | `generated/blocks/block_*.json` | `catalog.test.ts` parses `VM_BLOCK_TYPE_DEFINE` and `vm_block_ids.h`: pin shape and wire ids must equal the C |
| Device block ops and arguments | `generated/contracts/*.json`, `generated/devices/*.json` | `catalog.test.ts` (every variant resolves; the op code equals `SYS_DEVICE_OP` / the device header's `SYS_DEVICE_OWN_OP`), `palette.test.ts` (every variant compiles) |
| State sizes | `state_len` of `generated/blocks/block_*.json` | `templates/crosscheck.py` and `catalog.test.ts` compare it with the `.state_len` of each `VM_BLOCK_TYPE_DEFINE` (a `sizeof` pinned by a `_Static_assert`) |
| Device state layout | `catalog/layouts.ts` | `catalog.test.ts` lays out `vm_block_device_data_t` from `vm_block_device.h` |
| Expression opcodes and stack effects | enums + `vm_expr_ops` / `vm_bit_ops` in `vm_expr_core.h` | `expression.test.ts` compares the table to the header; formulas run on a reference stack machine |
| Limits | `generated/common/limits.json`: the options marked `#@limit` in the Kconfig of `vm_core` and `router`, with the values the firmware is built with (`templates/generate_limits.py`) | `limits.test.ts`: the file equals `sdkconfig`, `DEFAULT_LIMITS` equals the file |
| Frame size | `CONFIG_ROUTER_FRAME_MAX` (a limit) less `[seq][flags]` and the two keys of a VM command | `limits.test.ts` reads `REQ_HDR` from `router_dispatch.c`; a link that carries less passes `limits: { frameMax }` |

## Extending it

- **A new block:** a `block_<name>.json` in the firmware (with `state_len`, which `crosscheck.py` compares with the C) and `sync.sh`: it appears in the catalog with no engine change. A block whose data grows with what the user wrote has `state_variable` and a codec for its `editor`.
- **A new editor:** `createEngine({ codecs: [{ editor: 'my-editor', encode(ctx) { ... } }] })`; the block descriptor names it in `editor`, the user's data lives in `BlockInstance.editor`.
- **Another board:** its `generated/` carries its own `limits.json`; or `createEngine({ limits: { maxIn: 8, frameMax: 244 } })` for one figure (a BLE link that carries a smaller frame), or `new Engine({ definitions })` with your own descriptors.

## What the firmware publishes for the engine

Three things were once held in the engine and are now published and checked at the source: `state_len` of every block (descriptor field, compared with the C), the `op` of every device variant (the servo's too), and the limits (`generated/common/limits.json`). The engine reads them; the tests hold the published values against the C and `sdkconfig`.

`tests/engine/fixtures/vm-golden.json` is the load frames of the firmware host suite's builder (`vm_lib.Prog`, run against the board) for a sample program, written by `.claude/skills/runit-esp/scripts/gen_vm_golden.py` (run it with the ESP-IDF Python; it also writes the old app's copy). `wire/frames.test.ts` compares byte for byte. It pins the packing policy of that builder (400 bytes a frame, a value cut at 192), which is why `frameBudget` and `dataChunk` default to those: the board takes any frame up to `frameMax`.

## Known gaps

- Initial values are not "fresh" (VM_CORE.MD): a block triggered by its inputs does nothing until an input is written at run time. The engine does not hide that.
- The layout of `vm_block_device_data_t` and of the core blocks' fields (offsets, `raw_type`) is in the descriptors only where they publish it; the device struct is held in `layouts.ts` and checked against the header.
- `generated/common/limits.json` holds the values of the build in `sdkconfig`. Two boards built differently need their own file (or `limits` passed by the app); no command of the system or VM contract reports them at run time.

## Not done yet

Recovering a project from the program stored on the board, decompiling a formula back to text, telemetry parsing and run-time reads (a watch list), a debug build, the settings / BLE side. The wire layer has `subscribeFrame`, `execFrame`, `speedFrame`, `resetFrame`.

## Tests

`npm test` (vitest), `npm run typecheck`, from `runit-app/`. Tests mirror the modules under `tests/engine/`; `palette.test.ts` compiles every block and variant, `expression.test.ts` and `catalog.test.ts` read the firmware C source to catch drift.
