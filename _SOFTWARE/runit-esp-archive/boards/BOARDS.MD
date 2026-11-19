# boards — the boards as C

Not a component and not compiled: nothing in the firmware includes these files. They keep what the boards were as C, with the notes of what was measured: the reference for a board's profile (SYS_BOARD.MD; the profile itself comes from `board.json`).

| File | Contents |
|---|---|
| `runit_r1_devices.c` | The devices of the runIT PCB, rev 1, and of a devkit (the ESP GPIO only), with the set-up of the TCA6424A's pins 22 and 23 |
| `runit_r1_power.c` | The power layouts of the PCB and a devkit |
| `runit_r1_defs.h` | The PCB's device IDs, wiring and limits those files use |

What a profile must keep from them:

- The order: the ESP GPIO, the TCA6424A, the two TCA pins, then the other chips by dependency (a device can only use devices that exist).
- TCA pin 22 must be an output: left as an input it floats and floods the INT line.
- The DRV8962's limit of 2 A is an assumption; the wiring notes in the files were measured on the PCB.
