# data_matrix

What the module is and how it is used: `DATA_MATRIX.MD` (this folder). This file holds only the rules for changing it.

## Never break

- A switch only: it hands packets over unchanged and knows nothing of them. No role, session, tag, format, queue or task here; what needs one belongs to the port that has the logic.
- Synchronous: a send calls the targets' `pass` on the sender's task, outside the lock; the lock only guards the table.
- The port's contract (`core/utils/data_stream_port.h`) is written there and nowhere else; a module depends on that header only, never on the switch.
- The switch never names a module or a provider, and depends on none: they register their ports themselves (a provider at its CREATE).
- Op codes and `data_stream_link_t` are wire format: never renumbered.

## A change here also changes

- A command or its args: `router/contract_data_matrix.json`, then `sh templates/sync.sh`.
- The port's interface: `core/utils/data_stream_port.h`, SYS_DATA_PROVIDER.MD "Stream port", the `runit-esp` skill (`architecture.md`, the `utils` row).
