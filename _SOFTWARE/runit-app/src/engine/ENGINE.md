# The VM engine (for agents)

`runit-app/src/engine/`: everything an app needs to build on the runIT VM, with no UI, no DOM and no connection: the block palette, the user's variables and paths (accessors), block wiring, execution order, and the compiler that turns a project into the frames the board loads. Written from the firmware (`runit-esp/components/vm/`) and `generated/`.

```ts
const engine = createEngine()                       // bundled descriptors; new Engine({definitions, limits, codecs}) for your own
const result = engine.compile(project)              // { ok, diagnostics, frames, ir, arena, ids, order }
if (result.ok) await loadProgram(link, result.frames)                  // OPEN, objects, values, accessors, blocks (src/backend/BACKEND.md)
```

A `Project` is `{ variables, blocks }`, plain data. The engine holds neither a project nor a connection: data in, data out.

## Layout (dependencies point down)

| Module | What it is |
|---|---|
| `wire/` | `format/`: the wire format: object / accessor / block records (`records.ts`) and their inverse (`parse.ts`: load frames back into a program), the program (`program.ts`), value encoding (`codec.ts`, `bytes.ts`), types and the numbers firmware publishes (`types.ts`, `definitions.ts`). `load/`: how the board takes it: batching into frames (`frames.ts`), run-time writes and the telemetry reader (`runtime.ts`), the arena the board will use (`arena.ts`), what it refuses at load (`check.ts`), limits and the frame arithmetic (`limits.ts`). |
| `generated/` | The one place that reads `generated/`: the wire numbers (`wire.ts`), the board's limits (`limits.ts`) and the descriptor files (`index.ts`, Vite globs). Everything else takes parsed data as arguments. |
| `catalog/` | The block palette. `fromGenerated.ts` is the only file that knows the descriptor format (`vm-block` v3): it resolves enums, and a device variant's contract / device command into the variant's fields (the arguments), pins and outputs. `shape.ts` gives the pin shape the board checks (`shapeOf`). `layouts.ts`: where the values sit in a device block's state (the descriptor does not describe the struct). |
| `model/` | The project, one folder per subject. `variables/`: `variables.ts` (tree of values, folders, references; edits return a new tree) and `layout.ts` (variables as objects: ids, flags, initial data). `paths/`: `paths.ts` (accessors as the user sees them; parse, format, resolve) and `pathBuilder.ts` (the accessor builder, below). `program/`: `program.ts` (`BlockInstance`, `Source`), `pins.ts` (groups expanded), `diagnostics.ts`. |
| `graph/` | `arrange.ts`: execution order (wiring, then the user's order), gates, feedback, loop bodies made contiguous with their spans. |
| `expression/` | The formula component of the expression blocks (below). Depends on `catalog` types and the generated enums only; `compiler/` uses it. |
| `compiler/` | `compile.ts` (the pipeline), `accessors.ts` (paths to accessors); `block/`: a block's state bytes (`state.ts`, `values.ts`) and the codecs (`codecs.ts`: the extension point, `expressionCodec.ts`: the formula editors); `runtime.ts`: writing and reading variables of a compiled program. |
| `recover/` | `project.ts`: a stored program back into a project (below). Depends on everything above it; nothing depends on it. |

## Expressions

A built-in component, no UI. `engine.expression('expr')` is the formula language of that block (`undefined` for a block that is not a formula block). Which opcodes a block speaks is in its descriptor: `opcodes: { enum, constant }` names the published enum (`vm_expr_op_e` / `f32` for `expr`, `vm_bit_op_e` / `u32` for `expr_bit`); the opcode numbers, aliases and descriptions are read from that enum of `generated/common/enums.json`, so an opcode added in C reaches the block's language with no descriptor or engine change (the stack effect and the operator syntax of a new one are added in `expression/syntax.ts`). A block with `opcodes` gets the codec of its `editor` automatically.

```ts
const f = engine.expression('expr')!
f.operators; f.functions; f.constants         // what an editor lists: symbol / name, opcode, arity, precedence, the firmware's description
f.tokenize('min(in0, 2) > 1')                  // { tokens: [{kind, text, from, to}], error? } for colouring
const r = f.compile('in0 * 2 + in1', 2, (pin) => wired[pin])   // { ok, formula: {constants, code, depth, inputs} } | { ok: false, error: {message, from, to} }
f.state(r.formula)                             // the block's state bytes
f.decompile(state)                             // { ok, text } | { ok: false, reason }: RPN back to text
```

The text is infix with C-like precedence (float: `+ - * / mod % ^ == != < <= > >= and && or || xor not ! ?:`, `true`, `false`, `pi`, `e`; bit: `| ^ & << >> >>> ~`, hex and binary numbers) and `inN` for input N; every opcode that takes stack operands is also a function by its alias or opcode name (`min(a, b)`, `rol(a, 8)`). It compiles to RPN: operands first, `IN` / `K` with one operand byte, constants deduplicated, the stack depth checked against the board's 16. `decompile` prints with the fewest brackets and reads back as the same bytes. In a project the formula is `BlockInstance.editor = { text }`; the codec reports `expression-empty` / `expression-error` (with the character range) as diagnostics.

## The accessor builder

No UI and no engine of its own: it sits on `resolvePath`, so an editor sees exactly what the compiler and the board will. A path is edited as a `PathDraft` (the root variable and a chain of steps; a position read at run time may still be an empty slot).

```ts
const { draft, problem } = parseDraft(tree, 'table[se')   // tolerant: reads what it can of half-typed text, problem = { message, at }
inspectDraft(tree, draft)       // { resolution (what it names or why not), empty slots, complete }
nextSteps(tree, draft)          // children of the folder reached (by name and by position, dimensions), or the positions of an array
rootOptions(tree)               // top-level variables with their shape and one `[]` per dimension
positionOptions(tree, 'se')     // single numbers anywhere in the tree whose path matches: what a slot can read
appendStep / replaceStep / cutSteps / setRoot / draftForVariable    // edits return a new draft
formatDraft(tree, draft)        // text with names; an empty slot is `[]`
toPath(draft)                   // the `ObjectPath` the compiler takes; undefined while a slot is empty
```

An empty slot stands for the first entry while the draft is inspected, so what follows it can still be offered. `dimensionsOf` / `shapeText` read arrays of one to three dimensions (a value array, a folder of equal rows, of such matrices).

## Run time: writing and reading variables

`engine.write(result, [{ variable, value, element? }])` gives the frames of a runtime write (`SET_DATA` on a running program, applied at the next pass; refused for a `usr_protected` object); a long value is cut between elements to fit the frame. `engine.telemetry(result)` gives a `TelemetryReader`: `read(frame)` takes a telemetry frame without its stream byte (`[ROUTER_KEY_VM][SET_DATA][count]…`, the same records a write is made of) and returns `{ values, objects }`; a value has its object id, first element, bytes, type, decoded `value` and the user's `variable`. A heap object the board creates is announced (`ADD_OBJECTS`) before its values and the reader remembers its type. Checked against frames captured from a board.

`engine.watch(result, detail)` (`compiler/watch.ts`, `watchPlan`) is what to SUBSCRIBE to: `flow` the blocks' When done and every variable, `full` every block output too. A program compiled with `allEnableOuts` has a When done object for every block (without it only those something reads). The plan holds the ids within the board's limits (`subscribeMax`, and `subscribeTracked`: the objects its folders reach, which the board follows 8 levels deep, so a folder is one id), the flow first; `dropped` counts what did not fit. `targets` names what each object stands for: `variableKey(id)`, `outputKey(block, n)`, `doneKey(block)` (a block that writes a variable has both of its names on the one object).

## Recovering a project from the program on the board

`engine.recover(frames)` takes the program's load frames (`{ op, payload }`, as `readProject` gives them, without the router keys) and returns `{ project, diagnostics, parsed, exact, compileDiagnostics }`. `parseProgram` (`wire/format/parse.ts`) reads the records back into a program; `recoverProject` (`recover/project.ts`) rebuilds the project: the named objects become variables (a pointer is a folder when its children are the objects that follow it, depth first, else a reference), an accessor becomes the path of its root plus its steps, the objects without a name become the outputs of the block that writes them, and each block record becomes an instance: type, the operation and device of a device block, pins, enables, the fields read out of the state at their offsets, a formula decompiled to its text. `exact` says compiling the recovered project gives the very same program; the test does it for every block and variant of the palette.

What the program never held does not come back: descriptions, the names of blocks and of a switch's cases, ids (they are `v<object>` / `b<block>`), positions on a canvas. Two programs the board cannot tell apart come back as one: a reference to the very next object reads as a folder of one, and a block that reads another block's output which that block also writes into a variable reads that variable.

## How a project becomes frames

1. Variables are checked and laid out as objects (ids `0..n-1`, depth-first). A folder is a pointer object over its children, a reference one over its target; an empty folder has no object.
2. Blocks are arranged: a block runs after what it reads (outputs, enable-outs, variables a block writes); a loop block is followed directly by its body (everything gated by its enable-out and what depends on it, but for a block whose Run when comes from outside the loop, e.g. a branch reading what the body wrote: it runs after the loop, when its own gate fires, and so does what depends on the loop only through it) and carries `[start, end)` in its state.
3. Each block gets the objects it writes: the variable the user named for an output, else an object of its own (bool for flow, i32 for int, f32 for float; `usr_protected`), and one for its enable-out when something reads it.
4. Every reading pin becomes an accessor. A path is **folded into its root** where the walk is the same every time (`motors[1][2]` is root = the object of `motors[1]`, step `[2]`: the board's fast path), but not through a folder a `cell` pin names (clone re-links it), nor through a folder or reference marked `dynamic` (known only at run time), nor past a run-time position. Below such a container a name stays a `NAME` step (the board finds the child by its tag), and a step the project cannot follow is no error: `resolvePath` gives an `unchecked` target (type unknown) and the board checks it. Identical accessors are made once; an accessor a path reads is made before it (the board needs it loaded).
5. State is encoded (its length is the descriptor's `state_len`): a core block's fields at their offsets, a loop's span, a formula by its codec; a device block as `vm_block_device_data_t` (56 bytes): device id, op, idle op, `k[]` constants (the bits of a float for a scaled argument). `op` is the u16 the firmware builds: `(contract_id << 8) | n` for a contract command, `0xFF00 | n` for a device's own command, never the bare `op` of the descriptor.
6. The program is held against `wire/load/check.ts` (the loader's rules), then framed to fit the board's frame (`wire/load/limits.ts`); a record that cannot fit one is an error (`ERR_ROUTER_FRAME_LEN`). **A mistake in a project is a diagnostic, never a thrown error**; a program with any error has no frames.

## Where each fact comes from

| Fact | Source | Held against it by |
|---|---|---|
| Op ids, enum values, flags | `generated/common/enums.json`, `generated/services/contract_vm.json` | `wire/frames.test.ts` (against the firmware's own numbers) |
| Record layouts, arena rules, load checks | `runit-esp/components/vm/vm_core/loader/vm_wire.h`, `store/vm_store.c`, `block/vm_block_build.c`, `loader/vm_loader.c` | `vm-golden.json` frames byte for byte; the arena test replays the allocator |
| Blocks: pins, fields, offsets, variants | `generated/blocks/block_*.json` | `catalog.test.ts` parses `VM_BLOCK_TYPE_DEFINE` and `vm_block_ids.h`: pin shape and wire ids must equal the C |
| Device block ops and arguments | `generated/contracts/*.json`, `generated/devices/*.json` | `catalog.test.ts` (every variant resolves; the op code equals `SYS_DEVICE_OP` / the device header's `SYS_DEVICE_OWN_OP`), `palette.test.ts` (every variant compiles) |
| State sizes | `state_len` of `generated/blocks/block_*.json` | `templates/crosscheck.py` and `catalog.test.ts` compare it with the `.state_len` of each `VM_BLOCK_TYPE_DEFINE` (a `sizeof` pinned by a `_Static_assert`) |
| Device state layout | `catalog/layouts.ts` | `catalog.test.ts` lays out `vm_block_device_data_t` from `vm_block_device.h` |
| Expression opcodes and stack effects | enums + `vm_expr_ops` / `vm_bit_ops` in `vm_expr_core.h` | `expression.test.ts` compares the table to the header and runs formulas on a reference stack machine; `language.test.ts` covers the per-block language, tokens and the decompile round trip |
| Telemetry and run-time writes | `VM_CORE.MD` "Telemetry is the same command" (`vm_wire.h` records) | `tests/backend/telemetry.test.ts` decodes frames captured from a board and the write frame the board accepted |
| Recovering a project | the records of `vm_wire.h`, `layoutVariables` and the compiler read backwards | `recover.test.ts`: every block and variant of the palette compiles, is recovered and compiles to the same bytes (`exact`) |
| Limits | `generated/common/limits.json`: the options marked `#@limit` in the Kconfig of `vm_core` and `router`, with the values the firmware is built with (`templates/generate_limits.py`) | `limits.test.ts`: the file equals `sdkconfig`, `DEFAULT_LIMITS` equals the file |
| Frame size | `CONFIG_ROUTER_FRAME_MAX` (a limit) less `[seq][flags]` and the two keys of a VM command | `limits.test.ts` reads `REQ_HDR` from `router_dispatch.c`; a link that carries less passes `limits: { frameMax }` |

## Extending it

- **A new block:** a `block_<name>.json` in the firmware (with `state_len`, which `crosscheck.py` compares with the C) and `sync.sh`: it appears in the catalog with no engine change. A block whose data grows with what the user wrote has `state_variable` and a codec for its `editor`.
- **A new editor:** `createEngine({ codecs: [{ editor: 'my-editor', encode(ctx) { ... } }] })`; the block descriptor names it in `editor`, the user's data lives in `BlockInstance.editor`.
- **Another board:** its `generated/` carries its own `limits.json`; or `createEngine({ limits: { maxIn: 8, frameMax: 244 } })` for one figure (a BLE link that carries a smaller frame), or `new Engine({ definitions })` with your own descriptors.

## What the firmware publishes for the engine

Three things were once held in the engine and are now published and checked at the source: `state_len` of every block (descriptor field, compared with the C), the `op` of every device variant (the servo's too), and the limits (`generated/common/limits.json`). The engine reads them; the tests hold the published values against the C and `sdkconfig`.

`tests/engine/fixtures/vm-golden.json` is the load frames of the firmware host suite's builder (`vm_lib.Prog`, run against the board) for a sample program, taken from the firmware host suite's builder. `wire/frames.test.ts` compares byte for byte. It pins the packing policy of that builder (400 bytes a frame, a value cut at 192), which is why `frameBudget` and `dataChunk` default to those: the board takes any frame up to `frameMax`.

## Known gaps

- Initial values are not "fresh" (VM_CORE.MD): a block triggered by its inputs does nothing until an input is written at run time. The engine does not hide that.
- The layout of `vm_block_device_data_t` and of the core blocks' fields (offsets, `raw_type`) is in the descriptors only where they publish it; the device struct is held in `layouts.ts` and checked against the header.
- `generated/common/limits.json` holds the values of the build in `runit-esp/sdkconfig`. Two boards built differently need their own file (or `limits` passed by the app); no command of the system or VM contract reports them at run time.

## Not done yet

A watch list from the variables marked `subscribed` (which the compiler ignores; the app's live view follows every variable), the settings / BLE side. The wire layer has `subscribeFrame`, `execFrame`, `speedFrame` (the slow-motion factor and, when given, `step_ms`: the program time a stepped pass adds), `resetFrame`.

## Tests

`npm test` (vitest), `npm run typecheck`, from `runit-app/`. Tests mirror the modules under `tests/engine/`; `palette.test.ts` compiles every block and variant, `expression/expression.test.ts` and `catalog.test.ts` read the firmware C source to catch drift.
