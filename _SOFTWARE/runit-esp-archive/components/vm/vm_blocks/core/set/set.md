# Set

Copies the value of one object into another: **Source** into **Destination**. Whole structures are copied too, as long as both have exactly the same shape.

## Pins

| Pin | Meaning |
|---|---|
| Source (optional) | The object to read. Unwired: the block writes the **Value** constant instead. |
| Value (setting) | The constant written to the destination while Source is unwired, converted to the destination's type (rounded, saturated). |
| Destination (required) | The object to write. It is only named, never read. |
| ENO | Active on the passes the copy happened. |

## Rules

- **Enabled → Destination = Source**, on every pass. While enabled, the destination follows the source; a value written to it from the app is overwritten on the next pass.
- To copy **once**, enable the block with a pulse: an **Edge**, a **Periodic** tick, an **On event**. To keep a pulse until the block can act on it, put a **Latch** in between.
- With **Source** unwired the block writes **Value** into the destination's first scalar, on every pass while enabled.
- Source and destination must have the same types and the same element counts. There is no conversion; a mismatch is reported and nothing is written.
- The destination must be a writable object that no block uses as its own output.
- The only state is the constant.
