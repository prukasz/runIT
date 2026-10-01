# Clone

Like **Set**, but the destination is a tree this block builds for itself. It copies the **Source** into a new tree and hangs it on a **Pointer cell**; when the shape of the source changes (a different message layout), it builds a new tree to match.

Use it to keep a snapshot of a structure that can change shape, for example a parsed message.

## Pins

| Pin | Meaning |
|---|---|
| Source (required) | The object (or tree) to copy. The block runs when it is fresh. |
| Pointer cell (required) | An element of a pointer object the clone is attached to. |
| ENO | Active on the passes a copy happened. |

## Behaviour

- Memory for the clone is taken only when the shape changes. While the shape stays the same the values are just refilled.
- A clone is **fresh for one pass** only, then it is not news any more.
- The number of clones that can exist at once is limited; a program that needs more gets a clear error at run time.
