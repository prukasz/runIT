# VM — Design Questions (user-facing semantics)

Answer inline after each `**A:**`. Short is fine — "hold", "yes", "not v1".
Where options are listed they are a starting point, not a menu; overwrite freely.
Anything you mark **skip** I will decide and flag as an assumption.

Once answered, this consolidates into the target look + the execution architecture
in [[VM.MD]].

---

## 1. Triggers — what starts a stack

**Q1.** Which triggers exist in v1? (cyclic / GPIO edge / time-of-day / message
arrived / device event / on program start / manual "run now")

**A:** every 

**Q2.** Can one stack have more than one trigger ("on GPIO 4 rising **or** every
1 s")? Or exactly one, and you duplicate the stack if you want two sources?

**A:** yes then possible a dispatch block 

**Q3.** You said queue rather than coalesce. What is the depth, and what should
the user see when it overflows — silent drop with a counter, visible error, or
stack marked faulted?

**A:** like 30s-50s when overflow drop and system error 

**Q4.** Is there a dedicated "on program start" trigger for one-time setup, or is
first-run initialisation something each block handles itself?

**A:** System actions should handle devices etc, for blocks self detect first run, there can be run once, but actions can handle most of thigs related to device 

**Q5.** Can a stack run another stack explicitly (a "Run: <stack>" block)? If yes
— does the caller wait for it to finish, or fire and continue?

**A:** stack is self contained, there can ble onle EN flag as global variable that oher stack read 

**Q6.** Two stacks become ready in the same instant. Does the user control
priority, or is it fixed (e.g. declaration order)?

**A:** stack declaration + starvation counter 

**Q7.** Should a stack be individually enable/disable-able at runtime by another
stack ("turn off the whole night-mode stack")?

**A:** again decision by sole EN global variable instead of block flow one 

---

## 2. Flow inside a stack

**Q8.** Branching: does a block get two flow outputs (if / else, each with its own
sub-stack), or is it a single linear chain where each block just decides whether
the rest continues?

**A:** there is branch out -> there can be N branches then executed sequentialy -> for example block A has two wnablet outs at once -> first top branch then bottom 

**Q9.** Is there an explicit "stop here" block that ends the stack early? 

**A:**the EN decides, 

**Q10.** Loops inside a stack — "repeat 10 times", "for each item"? Or forbidden,
one pass per run, PLC-style?

**A:** For exist as block that poses subbranch that executes N times in one cycle

**Q11.** When a block mid-stack is inactive, everything below is inactive too.
Can a block opt out of that ("always run, regardless of flow")?

**A:**EN disables chain, always run is then separate stack that will execute always NULL on EN -> so automatically new stack 

**Q12.** Can the same block instance appear in two stacks, or is a block owned by
exactly one stack? 

**A:**you can access output data globally-> but ENo automatically joins block using it to that block stack creating tree. but sensor reading is stored globally 


**Q13.** Visual nesting — can a stack contain an indented group (a collapsible
sub-stack), or is it strictly flat?

**A:** Tree look with multiple roots and infinite branches 

**Q14.** "Every 100 ms" mid-stack can only mean *at most* every 100 ms. Two
separate blocks ("Every 100 ms", top-of-stack only / "Not more often than
100 ms", anywhere), or one block that changes meaning by position?

**A:** on average every 100 ms -> if once or twice cant execute in time execute asap. never more executions than defined by frequency, but some might be dropped and flagged by error -> time streching

---

## 3. Data and wiring

**Q15.** When a block produces a value, is it a named thing the user can reference
from anywhere, or reachable only by drawing a wire from that output?

**A:** Value can be label or wire link > link then gives EN also 

**Q16.** Are there user-visible global variables (a variables panel), or is all
state inside blocks?

**A:** yes normal user variables, aviable as labels  normal accessros 

**Q17.** Type mismatch on a wire (float out → integer in): silent convert, connect
with a warning, or refuse the connection?


**A:**ceil floor, warning on label / wire but must work

**Q18.** Can a data wire cross between stacks, or only within one?

**A:** block Data is global

**Q19.** Arrays/lists: does the user index and iterate them, or are they only
produced and consumed whole (e.g. a JSON blob passed along)?

**A:** depending on block input and output , when requiring single variable accessor must point when required an array pass an array

**Q20.** Constants: typed directly into the block face (`Set servo [90]°`), or
separate constant blocks that get wired in?

**A:** Constant are in block body where possible 

**Q21.** Reading a value whose producer has never run yet — zero, last known,
"invalid" that propagates, or a load-time error because it is unreachable?

**A:**  preinitialiation with data when sending object

**Q22.** Does the user ever see a "type" at all (float/int/bool/string), or is it
inferred and hidden entirely?

**A:** Will be visible -> when option is set to ON: allow user to dont care or have entire freedom though string guarded from numeric

---

## 4. Time

**Q23.** Is the base scan rate user-visible and settable, or hidden entirely? 

**A:** as fast as possible -> so dont waste time 

**Q24.** "Wait 50 s": does the stack conceptually sit inside the wait (only that
stack pauses), or is it a gate that passes only after 50 s of continuous enable
while everything keeps sweeping?

**A:** Gate

**Q25.** If the enable drops at 30 s into a "wait 50 s", does the timer reset to
zero or hold at 30?

**A:** resets 

**Q26.** What timing precision should a user be promised? Is 1 ms meaningful, or
is 10–50 ms the honest floor for anything they can express?

**A:** 10ms 

**Q27.** Wall-clock: does the user get real dates/times ("at 07:00", "on
Mondays"), or only relative time ("after 50 s")?

**A:** when time sync possible then separate block 

**Q28.** Should long durations survive a reboot (a 6-hour timer mid-count), or
restart from zero?

**A:**  depends on nvs field

---

## 5. Actuators and inactive behaviour

**Q29.** Default for "when inactive" on an actuator: **Hold**, or force the user
to choose before the program validates?

**A:** chose 

**Q30.** Is there a global safe-state / e-stop that overrides every actuator at
once, independent of the program?

**A:** look at actions action can be invoked by blocks

**Q31.** On program reload, what happens to outputs during the swap — hold last
value, go safe, or undefined-but-brief?

**A:** look at actions again

**Q32.** After a power cycle, do outputs restore their previous values or always
start from safe?

**A:** look at states and nvs 

**Q33.** Two stacks writing the same servo: refuse at load (single writer
enforced), allow with last-writer-wins, or allow with a visible warning?

**A:**  allow with note

**Q34.** Does an actuator block need a "current actual value" input (feedback), or
is it write-only in v1?

**A:** stores internally 

---

## 6. Errors

**Q35.** A block fails at runtime (sensor read times out). Does the stack
continue, stop there, or is it a per-block choice on the block face?

**A:** per block, but generally actions will hanlde as block will call error chain

**Q36.** Should a failing block automatically stop the flow below it, or pass the
flow on and let the user handle it?

**A:** keep as options 

**Q37.** Where does the user see errors — live on the canvas (block turns red), a
log/list, or both?

**A:** on canvas along with subscirbed valuses as well as details logs 

**Q38.** Is retry a user-visible concept ("retry 3 times then fail"), or handled
invisibly by the driver?

**A:** generate error but user chose wether to stop code or jsut ignore 

**Q39.** Is there an "on error" trigger so the user can build their own reaction
stack? 

**A:** Yes but prefer action

---

## 7. Program lifecycle

**Q40.** Editing while running: can a single block be patched live, or is every
change a full program reload?

**A:**  patched live along with any alloved object - data only 

**Q41.** Does state survive a reload — does a counter at 47 stay at 47 after the
user edits an unrelated block?

**A:** NVS decides

**Q42.** What survives a power cycle? Does the user pick per value ("remember
this"), or is it automatic/none?

**A:** Simplified(automatic) or manual (so selec)

**Q43.** Does the device hold one program, or several that can be switched?

**A:** can just hold packets then reload (like action)

**Q44.** Is there a dry-run / simulate mode where logic runs but outputs do not
move? 

**A:** then "freeze mode of drivers" 

---

## 8. Visibility and debugging

**Q45.** Are live values shown on the canvas while running, or only on request?

**A:** subscribed id 

**Q46.** Can the user force/override a value for testing, and if so does that need
to be visually obvious and time-limited?

**A:** !!!IMPORTANT THATS A part of remote control but only after cycle never mid

**Q47.** Step-through or breakpoints — genuinely useful for this audience, or too
much for the entry point you want?

**A:** Mode run by step, run one scan 

**Q48.** Trends/history of a value over time — in scope, or a separate concern?

**A:** totally not mcu problem 

---

## 9. Scale

**Q49.** Realistic upper bound on blocks in one program for your target user —
50? 200? 1000?

**A:** 300 should be max

**Q50.** How many stacks would a typical program have, and how deep does one get?

**A:** totally flexible no more than blocks 
