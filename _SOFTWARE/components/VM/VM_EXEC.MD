# VM Execution Model & Target Look

Consolidated from [[VM_DESIGN_QA.md]]. This is the **design target**, not what is
built — the storage, object, accessor and block layers exist ([[VM.MD]]); nothing
in this document executes yet.

---

## 1. The user's model

### Three kinds of connection

A user never draws an enable wire as a separate thing, and never sees the words
EN or ENO. What they draw carries the meaning:

| gesture | ordering | gating |
| :--- | :--- | :--- |
| wire into a **data port** (`MAP.OUT → SERVO.ANGL`) | yes | no |
| wire into the **EN port** (`A>B.RES → SERVO.EN`) | yes | yes |
| **label** reference (`G_B → A>B`) | no | no |

A data wire says "run after this, and read its value". An EN wire additionally
says "…and only if it passed the flow on". A label is a pure global read — no
ordering, no gating — the escape hatch for "I want the sensor reading but I am
not part of that chain".

Note **ungated and unordered are different**. `MAP` has a data wire in and no EN,
so it always runs, but still runs after the `ADC` feeding it. Only a block whose
every input is a label has no predecessors at all.

### Two activation groups

Some blocks only make sense when something *new* arrives — an event router has
nothing to do if no event came. Forcing those to carry an EN wire alongside the
data wire would mean two wires between the same pair of blocks, every time, for
no added information. So the block type declares how it activates:

| group | activates when | EN port shown | examples |
| :--- | :--- | :--- | :--- |
| **update-driven** | a declared trigger input is *fresh* | optional | `CASE IO`, event/message routers |
| **enable-driven** | its EN sources say so (`en_cnt == 0` ⇒ always) | yes | `SERVO ANGLE`, `SET PIN`, `TOGGLE PIN` |

The two **compose**: a block may be both, and then activates only when a trigger
input is fresh **and** its EN list allows — "route this event, but only while
armed". So `trigger_mask` and `en_cnt` are independent, not a choice between two
modes.

`ADC`, `MAP`, `A > B` and expression blocks are enable-driven with no sources, so
they simply always run. No third category is needed.

**A block that does not activate drives its outputs to their defaults** — it does
not merely skip. Skipping would leave the previous pass's values standing, so a
router that fired once would leave its `YES` output true forever and everything
below it would keep running on a stale flow.

That clearing must be **quiet**: it writes the default *without* marking the
object fresh. A clear that set `upd` would look like new data to any
update-driven block downstream, and one event would ripple outward for a second
pass. So the non-activation path needs a write that bypasses the `upd` set in
`vm_store_inline()`, or a clear immediately after it.

### A forest of graphs

Multiple roots, arbitrary branching, and **merges** — a block may have several
incoming wires. Each root carries a **trigger** on its face, the only scheduling
concept the user ever sees:

```
┌─ On GPIO 4 rising ────────┐   ┌─ Every 100 ms ───────────┐
│  Read sensor      → [v]   │   │  Read pressure   → [p]   │
│  Wait 50 s                │   │  If p > 2.0              │
│  If v > 100               │   │    └ Set valve  closed   │
│    ├ Set servo   90°      │   │       when inactive: Hold │
│    └ Set lamp    on       │   └──────────────────────────┘
└───────────────────────────┘
```

Branches run **top to bottom, depth first**. Position on the canvas is a
*tie-break*, not the rule: data dependencies win, so a block always runs after
whatever feeds it regardless of where it sits. Top-to-bottom decides only between
blocks the ordering leaves free.

The editor should therefore lay out (or warn) so visual order always matches
computed order — it runs the sort anyway. Otherwise a user can drag a block above
its own producer and the canvas will be lying about what happens first, which is
the one thing this layout is supposed to guarantee.

### One primitive behind four features

"Wait 50 s", "Every 100 ms", "If A and B", "Stop here" are all the same thing: a
block that decides whether the flow continues below it. No scheduler involvement,
no new mechanism, and they compose in any order because each only knows about the
blocks feeding it.

### Everything else the user sees

* **Activate when** is a visible field on the block face, listing the incoming
  links from other blocks. Node-style: **any** of them arriving activates the
  block. The user reads what enables a block off its own face rather than
  tracing wires backwards.
* **Constants** live in the block face (`Set servo [90]°`), not as wired blocks.
* **Types** are visible, and permissive by default — float→int rounds, with a
  warning badge on the wire. A strict mode turns warnings into refusals. Strings
  are always guarded from numerics.
* **Actuators** carry a visible `when inactive:` choice, and the program does not
  validate until it is chosen — no silent hold, no silent home.
* **Errors** appear on the canvas on the failing block, alongside subscribed live
  values, with detail in a log.
* **Live values** appear for explicitly subscribed ids, not everything.

---

## 2. Execution architecture

### Where order comes from

Merges make the flow graph a **DAG, not a tree**. The client resolves it before
upload: walk the wire graph, find blocks with no incoming flow edge (roots),
**topologically sort** each connected component, and emit it as an ordered list of
block ids. Canvas position breaks ties between blocks the sort leaves unordered,
so the order the user reads top-to-bottom is the order that runs.

**The device never sorts.** The whole forest — standalone blocks and long chains
alike — merges into **one** top-to-bottom order for the program. There is no
per-section ordering, because there is only one order.

That makes the **block registry index the execution order**: the client sorts,
uploads its `0x45` frames in that order, and the supervisor walks ids `0..n-1`.
`FOR` carries its `[start, end)` range in its own `custom_data`, since a span is
the one thing a flat order cannot express.

### Sections are atomic units, not schedules

Sections still exist, for a different job than ordering: a section is the unit
that **runs all at once**, and therefore the only place the system may be
interrupted. Freeze, resume and the double-buffered swap take effect at a section
boundary — never mid-section, where half the blocks would have acted on new
inputs and half on old.

So a section is a `[start, end)` range over the one global order, exactly like a
`FOR` span. It says nothing about *whether* or *when* its blocks run — every
section runs every pass — only that once started it finishes before anything may
intervene.

It also settles label ordering. A label read *below* its writer sees this pass's
value; *above* it, last pass's. Not a race — a position, readable off the canvas.

### Enable is a list, with a mode

A block carries **0..N enable sources** — surfaced to the user as the block's
`activate when` field:

| sources | meaning |
| :--- | :--- |
| 0 | root — always enabled |
| 1 | ordinary chain link; mode is irrelevant |
| N | `any` — branches rejoining, either path reaching the block runs it |
| N | `all` — independent conditions, every one must hold |

Both modes are needed and neither is a safe default for the other. Two branches
of one split rejoining is an OR. "On rising edge **and** while `A > B`" — the
sample-on-trigger pattern — is an AND, and running it as an OR would move the
actuator on every edge regardless of the condition. The canvas draws both as
wires into `EN`, so the mode is a visible selector on the block face rather than
something inferred from shape.

A source that fails to resolve counts as *not* enabled, so the fail-closed rule
survives: under `any` one broken branch cannot mask a working one, and under
`all` a broken condition correctly stops the block.

Absence and unresolvability are opposites on purpose — zero sources is a root and
always runs, while a source that exists but cannot be read blocks.

### Freshness is consulted on declared trigger inputs only

An update-driven block must not fire because some unrelated global it reads
happened to be written. So the *wiring* does not decide which inputs activate —
the **block type** does, in its palette descriptor:

```c
typedef struct {
  void (*execute)(vm_block_h);
  uint16_t trigger_mask;   // pins whose freshness activates; 0 = enable-driven
  uint16_t custom_len;
  uint8_t in_min, in_max, q_cnt;
} vm_block_desc_t;
```

`CASE IO` declares `trigger_mask = 0x1` — its `EVENT` pin. Every other input it
reads is inert however often it is written. The descriptor is `const`, per *type*,
in flash, so this costs **no RAM per block** and no struct field: a mask in
`vm_block_data_t` would be identical across every instance of a type.

It also collapses the two groups into one field — `trigger_mask == 0` is
enable-driven, non-zero is update-driven.

One editor rule: **a trigger pin must be fed by a wire, not a label.** A label
gives freshness without ordering, so the block could observe the update a pass
late — a race, and a confusing one.

### `upd` is cleared by the supervisor, not the consumer

Freshness rides on `head.f.upd`, already set by every write. The clearing rule is
the part with a failure mode: if a consumer cleared the flag when it ran, fan-out
would break — two blocks reading the same fresh object, and whichever ran first
would steal the update.

So the supervisor clears `upd` at the end of a **pass** — not at the point of
consumption, and not at each section boundary. Topological order already
guarantees a writer runs before its readers within the same pass, so every
consumer sees the flag before it is cleared. Clearing per section would break
that across sections: a value written in section 0 would no longer read as fresh
in section 3, even though section 3 runs after it in the same pass. Cost is one
sweep of the object registry per pass.

**The sweep must respect `upd_resetable`.** That header bit already exists and is
carried over the wire (`VM_LOAD_F_UPD_RESETABLE`). A non-resettable object is
never cleared by anyone — the flag is reserved for constants and user variables,
where freshness is not a meaningful signal.

A non-resettable object *may* still be used as a trigger input — but since its
`upd` never clears, the block reading it is permanently fresh. That is not
rejected; the block is simply **marked always-active** in the editor, so the user
sees the consequence on the block face rather than being blocked by a rule or
surprised by a block that never stops firing.

Order within the pass matters: **run sections → sample subscriptions → clear
`upd`.** Clearing before telemetry would silently break "send on update", which
is the flag's other customer.

### Event payloads are valid for the pass that published them

An event object holds the *latest* payload, not the one that activated a given
block. Within a pass that is the same thing, so linking an event output onward
freely is fine and needs no copy. Across passes it is not: anything behind a time
gate resumes later, by which point a newer payload may have landed. There, take
an explicit **Copy** in the triggering pass.

That is a rule the editor can check exactly — if a path from an event output to a
consumer crosses a time-gate block, require a Copy — which is far narrower than
banning the link outright.

### Triggers are blocks, not section headers

An earlier draft gave each section a trigger descriptor and routed events to
sections through a lookup table. The update-driven group removes the need for all
of it.

`ON EVENT` is an ordinary block that pops **one** queue entry per pass and
publishes it to its output, setting `upd`. `CASE IO` fires because its trigger
input is fresh. Nothing had to be scheduled: no trigger descriptor, no
event→section routing, no per-section run decision.

The queued semantics survive for free — three queued messages become three
passes, each publishing one payload and driving the chain once with distinct
data. "N events → N runs" with none of the machinery.

Time-of-day, GPIO edge, program start and manual run are all the same shape:
blocks that publish, or that pass the flow on when their condition holds.

### The supervisor loop

Free-running — power management is explicitly not a goal. The pass alternates
between running a section and a **freeze point**:

```
freeze → run section 0 (atomically) → freeze → run section 1 → … → end of pass
```

A section runs to completion with nothing intervening. Everything external
happens at a freeze point: pending events drained into their event blocks,
remote overrides applied, live data patches committed, the buffer swap taken.
Freeze and resume take effect there too, which is the point of section
granularity — freeze latency is bounded by the longest *section*, not by a
whole pass over 300 blocks.

At end of pass: sample subscriptions, then clear `upd`.

There is no scheduling policy to state and no starvation to counter — every
section runs every pass, and the work saved is inside the blocks rather than in
choosing between them.

**Runs on core 1.** The VM gets the second core so a tight loop cannot starve
BLE, which lives on core 0. That is not quite enough on its own: ESP-IDF watches
the idle task of *both* cores (`CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=y`,
5 s), and `taskYIELD()` will not feed it because idle runs at priority 0. So the
loop yields one tick per pass — with `CONFIG_FREERTOS_HZ=100` that is exactly the
10 ms floor already promised, and it leaves core 1 usable by device drivers.

Because there is no sleep, timing needs no deadline heap and no timer to re-arm:
time-based blocks simply compare timestamps on each pass, and the 10 ms floor is
set by how fast a full pass completes, not by a tick.

Everything runs on the supervisor's own task. A callback or ISR only enqueues; it
never touches a block, an object or the arena. That keeps the entire
store/accessor layer lock-free, and it is why "override only after a cycle, never
mid" falls out automatically: overrides, patches and events all apply at the same
drain point, so a section always traverses a consistent snapshot.

### Within a section

Every block in a running section is called, including inactive ones — that is what
makes `when inactive:` work at all. Inactive means the block runs its disabled
branch, which for most blocks is "do not pass the flow on", pruning everything
downstream by data rather than by control flow.

**FOR is a supervisor of its own sub-range.** It owns a `[start, end)` span of the
section's ordered list and walks that span N times within one pass. N must be
bounded at load so a section's worst-case duration stays computable.

Two consequences for the top-level walk:

* **It must jump over a FOR's range**, not fall into it. Otherwise every block in
  the span runs once from the outer walk *plus* N times from the FOR.
* **Nesting needs a depth bound**, for the same reason accessor chains have one:
  a section comes off the wire and nothing stops it declaring FORs inside FORs
  until the stack is gone.

This is why a section packet has to carry extents, not just a flat id list —
pruning works without structure (a disabled block's dependents self-skip), but
repetition genuinely needs the walker to know where a span ends.

### Transient activation needs a latch to become a level

An event enables for exactly one pass, so anything time-based below it can never
accumulate: a `Wait 50 s` downstream of an event sees its enable drop the very
next pass and resets to zero. Event → wait → act is therefore not expressible
directly.

The palette answers it with **latch** blocks (and pulse blocks with a negated
output), which the user places explicitly to convert a one-pass pulse into a held
level. The same block solves actuators below an event, where `when inactive:` is
otherwise the only thing keeping the output from snapping back on the next pass.

Nothing prevents the mistake — a `Wait` below an event simply never completes.
The diagnosis route is **subscribing to the block's state**: the user watches the
gate and sees it never advancing, rather than being warned up front. That is a
debugging surface rather than a guard, and it is the sharpest edge in the model.

### Timing

* Honest floor is **10 ms**; nothing finer is promised to the user.
* "Wait 50 s" is a **gate**, not a suspension — the section keeps running, the
  block passes the flow only after 50 s of continuous enable. Enable dropping
  **resets** the timer to zero.
* "Every 100 ms" is average-rate with **time stretching**: the deadline advances
  by a fixed period so there is no drift, a late run goes as soon as possible,
  and a run that would exceed the declared frequency is **dropped and flagged**
  rather than bursting to catch up.
* Durations surviving reboot is per-value, decided by the NVS retention flag.

### Events and the queue

Queued, never coalesced — N events produce N passes, so N distinct payloads are
actually processed. Each entry carries `{route, payload}`, where the route names
the event *block* that owns it, not a section. That block publishes one entry per
pass into its output object, after which everything downstream is ordinary
persistent state reached by ordinary freshness.

> **Activation is transient. Data is persistent.**

That one rule is what makes "runs once when the JSON arrives, but the JSON stays
readable" non-contradictory.

Depth is a fixed **number of entries**. Overflow **drops and raises a system
error** — never silent, never unbounded.

### Memory: the client sizes everything

Every pool is allocated **before** blocks load, at exactly the size the client
declares — per element kind (objects, accessors, blocks, sections), in the open
packet. The device does not estimate and does not grow anything. A block header
is **16 bytes** plus 4 per input, output and enable source.

The real limit is therefore contiguous internal DRAM at load time, not a
compile-time budget. A program that does not fit fails cleanly with
`ERR_VM_LOAD_TOO_BIG`, carrying both what was requested and the largest block
actually available, so a fragmented heap is distinguishable from a genuinely
oversized program. `VM_STORE_MAX_POOL` (128 kB) is only a sanity ceiling against
a corrupt declared size.

### Guarded objects, per-block error policy, and the block watchdog

Three enforcement points, all with the state already in place and the check
missing.

**`usr_mutable` guards objects user logic must not write** — a block's own ENO or
result. The flag is set at load and stored in the header, but nothing reads it:
`vm_resolve_fast()` and `resolve_d()` check `mutable` only, so a user-authored Set
block can currently write another block's ENO. Enforcing it means the write path
has to know who is asking — internal writes check `mutable`, user-authored and
remote writes check `usr_mutable` as well.

**On-error behaviour is a per-block flag** — `cfg.on_error`, `VM_BLK_ERR_STOP`
(publish a false ENO, so everything downstream self-skips) or
`VM_BLK_ERR_CONTINUE`. **Implemented** in the struct and the wire format; what
enforces it is the supervisor, which does not exist yet.

**One global watchdog, not per-block timing.** Timing every block would cost a
clock read per block per pass for something that almost never fires. Instead the
supervisor publishes *which block it is currently executing* to a volatile, and a
timer ISR samples it: if the same block is still current after ~20 ms, raise the
alarm. Cost is one store per block and no clock reads at all, and unlike the task
watchdog it names the block that hung instead of just resetting.

Two things to get right when building it. The ISR must distinguish "same block
still running" from "same block again next pass" — a monotonically increasing
sequence number alongside the block id, rather than the id alone. And the ISR
must not *report* the error itself: error construction allocates, which is not
ISR-safe. It sets a flag and something at task level builds the trace.

### Invoking an action

A block type that calls `sys_actions_invoke(id)` with the id in its own
`custom_data`. That is the whole VM↔actions binding: safe states, e-stop,
freeze and power-on behaviour are [[SYS_ACTIONS.MD]]'s, and the VM reaches them
through one ordinary block rather than any dedicated mechanism.

### Conflicting writers

Two sections writing the same actuator is allowed. For hardware blocks the
**last writer in execution order wins** — deterministic, since section order and
in-section order are both fixed at load. The editor should still note the overlap,
because it is far more often a mistake than an intent.

### Safety belongs to actions, not the VM

Device-level safe state, e-stop, freeze-on-reload and power-on behaviour are
[[SYS_ACTIONS.MD]] concerns — ids 1–5 already bind the device sweeps
(freeze/resume/suspend/reset/hard-reset). A block invokes an action; the VM does
not own actuator safety.

This gives dry-run for free: **freeze mode** is the driver sweep, so logic runs
and outputs do not move.

### Lifecycle

* **Live patch** of data-only objects while running — applied at the drain point,
  same as an override. Structural change is a full reload.
* **Retention** is per-value, either automatic (simplified) or hand-picked.
* The device holds **one active program**, plus stored packet sets that can be
  replayed to switch — the same mechanism as an action.
* **Step mode**: run exactly one scan, then stop.
* Trends/history are explicitly out of scope for the MCU.

---

## 3. What this changes against the current code

| | now | needed |
| :--- | :--- | :--- |
| Enable | ~~single `cfg.en` accessor~~ | **done** — `en_cnt` + list + `en_mode` (any/all), header 20 → 16 B |
| Section id on a block | ~~`cfg.section_idx`~~ | **done** — removed; the section's list is the only copy |
| Unwired input | ~~loader rejected every unresolvable id~~ | **done** — `VM_BLOCK_NO_ID` ⇒ NULL slot; `connected_in` dropped |
| On-error | ~~nothing~~ | **done** — `cfg.on_error` in struct + wire; supervisor must honour it |
| Sections | do not exist | `[start, end)` ranges over the global order — atomic units for freeze/resume, *not* schedules |
| Non-activation | nothing | quiet write of defaults to flow outputs (no `upd` set) |
| Latch / pulse | nothing | palette blocks — the only way transient activation reaches a time gate |
| `block_type` | copied, only used in error payloads | bounds-check against the palette **at load**, with `custom_len` |
| `usr_mutable` | set at load, read by nobody | checked on user-authored and remote writes |
| Watchdog | nothing | volatile current-block + sequence, sampled by a timer ISR at ~20 ms |
| Clock | nothing references one | needed by every timing block |
| Dispatch | `block_type` stored, nothing reads it | const descriptor table in flash (incl. `trigger_mask`), one `.c` per block type |
| Queue | none | `{route, payload}`, fixed entry count, drop + error |
| `upd` | set on write, read by nobody | supervisor clears every pass, after telemetry sampling |
| Open packet | `obj_cnt, acc_cnt, blk_cnt, total_size` | plus a section count |
| Loader | `OPEN` → build → done | accept `0x43` against a *running* program, applied at drain |
| Retention | none | NVS, keyed by name not id |

---

## 4. To measure, not to decide

Nothing in the model above is open. One number is worth measuring before the
palette is sized: a ~300-block program of the shape [[VM.MD]]'s budget assumes
wants roughly **77 kB in one contiguous internal-DRAM block**. That is under the
128 kB ceiling and the failure mode is already clean — the load is rejected with
requested-vs-largest-available — so this is a deployment reality to check on real
hardware, not an architecture problem to solve.
