# Set

Copies the value of one object into another: **Source** into **Destination**. Whole structures are copied too, as long as both have exactly the same shape.

## Pins

| Pin | Meaning |
|---|---|
| Source (optional) | The object to read. The block runs when it is **fresh** (written during this pass). Unwired: the block writes the **Value** constant instead. |
| Value (setting) | The constant written to the destination while Source is unwired, converted to the destination's type (rounded, saturated). |
| Destination (required) | The object to write. It is only named, never read. |
| ENO | Active on the passes the copy happened. |

## Rules

- Source and destination must have the same types and the same element counts. There is no conversion; a mismatch is reported and nothing is written.
- The destination must be a writable object that no block uses as its own output.
- With **Source** unwired the block writes **Value** into the destination's first scalar: on each pass an enable fires (an open gate, a tick), or once at start when no enable is wired. The destination never triggers it.
- When enables are wired, the block also runs on each pass an enable fires (an open gate, a tick).
- The only state is the constant and a flag that it has been written.
