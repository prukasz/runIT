# Run action

Runs a stored action (a record of the action store: a list of commands) once each time its enable turns on: a user action, a system setting or a device's defaults.

## Pins and settings

| | Meaning |
|---|---|
| Action id (pin, optional) | The record to run. Unwired: the Action setting. |
| Action | The record's id, used while the pin is unwired; it cannot be 0. |
| ENO | A one-pass pulse on the pass the request was queued. |

## Behaviour

- It fires on the **rising edge** of its enable: a pulse from **Edge**, **Every** or **On event** fires it once per pulse. A block with no enable sources fires once, when the program starts.
- The action does **not** run inside the pass. It is queued and run on the router's task, because an action may stop, rewind or unload the program itself and a long replay would outlast the block watchdog. Its own result is reported afterwards, not by this block.
