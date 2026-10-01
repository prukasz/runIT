# On event

Starts a chain of blocks when a matching **system event** arrives (for example a pin change or a power alert).

## Settings

Four filters that must all match; each can also be *any* (255):

- **Domain**, **Device**, **Channel**, **Event**.

## Pins

| Pin | Meaning |
|---|---|
| ENO | A one-pass pulse on the pass in which a matching event arrived. |
| Value | The value of the event (the last match of the pass). |
| Count | The number of matching events this pass. |

## Behaviour

- Events reach the program only through a subscription routed to it; this block does not subscribe by itself.
- An event lives exactly **one pass**. An event that arrives while the block is disabled is missed, like a physical edge. Several blocks can match the same event.
- The pulse is one pass long. To hold it (event, then wait, then act), put a **Latch** below it.
