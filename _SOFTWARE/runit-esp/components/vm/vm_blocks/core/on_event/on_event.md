# On event

Starts a chain of blocks when a matching **system event** arrives (for example a pin edge, an over-current alert or a motor fault).

## Settings

Three filters that must all match; each can be *Any*:

- **Device**: the device that raised the event.
- **Event**: what happened. The events a device can raise depend on the contracts it has (IO, power, H-bridge); with the device *Any*, any event.
- **Channel**: the pin or channel of that device the event came from.

Changing the device puts what it narrows back to *Any*.

## Pins

| Pin | Meaning |
|---|---|
| ENO | A one-pass pulse on the pass in which a matching event arrived. |
| Value | The value of the event (the last match of the pass). Several matches in one pass collapse into one pulse. |

## Behaviour

- The program listens to every event; this block only filters them. Nothing is consumed: every On event block sees every event of the pass.
- An event lives exactly **one pass**. An event that arrives while the block is disabled is missed, like a physical edge.
- The pulse is one pass long. To hold it (event, then wait, then act), put a **Latch** below it.
