# Clone

Like **Set**, but the destination is a tree this block builds for itself. It copies what is on **Copy from** into a new tree and hangs it on **Copy to** (a pointer cell); when the shape of the source changes (a different message layout), it builds a new tree to match.

Use it to keep a snapshot of a structure that can change shape, for example a parsed message.

## Pins

| Pin | Meaning |
|---|---|
| Copy from (required) | The object (or tree) to copy. |
| Copy to (required) | An element of a pointer object the clone is attached to. |
| ENO | Active on the passes a copy happened. |

## Behaviour

- It copies on every pass while enabled. Memory for the clone is taken only when the shape changes; while the shape stays the same the values are just refilled.
- To take a snapshot **once** (keep a message while the source moves on), enable it with a pulse: an **Edge**, an **On event**, a **Periodic** tick.
- The number of clones that can exist at once is limited; a program that needs more gets a clear error at run time.
