# Power mux (ideal diodes)

Chooses which of two supplies feeds a load, for example the voltage input of an H-bridge. On the board there is one such pair of LM73100 ideal diodes for each DRV8962 motor driver.

## On this board

The two H-bridges each have their own power mux, loaded with the board: **SUPPLY_DRV8962_0** (ID 7, DRV #1) and **SUPPLY_DRV8962_1** (ID 8, DRV #2). **Source A is always the board input (VSUP); source B is the regulator rail of that bridge**: rail B (TPS55289 0x75) for DRV #1, rail A (TPS55289 0x74) for DRV #2. Both are off at boot; open the mux in the Board view, add **Select source** to its default settings and save the hardware config.

## Install your own

Add a power mux for any other load and give it its two enable pins:

| Setting | Meaning |
|---|---|
| Enable source A | The pin that switches source A on (on this board a pin of the TCA6424A). |
| Enable source B | The pin that switches source B on. |
| Enable level | 1: a high pin switches the source on (the LM73100). |
| Switch delay | Pause, in milliseconds, between switching the old source off and the new one on. |

After the install **both sources are off**: nothing feeds the load until you choose a source.

## Choose the source

Open **Default settings** of the power mux and add **Select source**: *Off*, *Source A* or *Source B*. Default settings are stored with the hardware config, so the board applies the choice at every boot, right after the installs. The same command is in **Commands** to try it live.

Which supply is A and which is B follows your wiring: on this board DRV #2 has A = rail A (TPS55289 0x74) and B = the board input, DRV #1 has A = the board input and B = rail B (TPS55289 0x75).

## Behaviour

- At most one source is on. Changing the source switches the old one off first, waits the switch delay, then switches the new one on.
- A fault that suspends the device switches both sources off; resuming brings back the chosen source.
- Make sure the chosen rail is on (a regulator that is disabled gives the load nothing) and stop the bridge before you change the source of a running one.
