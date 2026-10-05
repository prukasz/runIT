# On event

Starts a chain of blocks when a matching **system event** arrives (for example a pin change or a power alert).

## Settings

Four filters that must all match; each starts at *Any*:

- **Domain**: the kind of event (IO, power, H-bridge, Bluetooth).
- **Device**: the devices that publish in that domain.
- **Channel**: the pins or channels the chosen device's events carry.
- **Event**: what the chosen device raises (its own events), or every event of the domain while the device is *Any*.

Changing the domain or the device puts what it narrows back to *Any*.

## Pins

| Pin | Meaning |
|---|---|
| ENO | A one-pass pulse on the pass in which a matching event arrived. |
| Value | The value of the event (the last match of the pass). Several matches in one pass collapse into one pulse. |

## Behaviour

- Events reach the program only through a subscription routed to it; this block does not subscribe by itself.
- An event lives exactly **one pass**. An event that arrives while the block is disabled is missed, like a physical edge. Several blocks can match the same event.
- The pulse is one pass long. To hold it (event, then wait, then act), put a **Latch** below it.
