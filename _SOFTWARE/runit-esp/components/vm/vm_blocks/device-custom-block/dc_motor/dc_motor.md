# DC motor

Runs a DC motor at a speed: it gets there along its ramps, goes through stop when it changes direction, and can stop, brake or coast. Reads the speed it runs at, its current and whether it stalled.

Works on a DC motor device. How the motor behaves is the device's settings, not the block's: its ramps up and down, the dead zone round stop, the speed where it starts turning, its top speed, the trim, the pause between directions and the stall current. The block only tells it what to do.

## Settings

| Setting | Meaning |
|---|---|
| Device | The motor this block works on. |
| Operation | What the block does with the motor; it decides the inputs and outputs below. |
| When disabled | Sent once when the enable turns off: stop (along the ramp), brake, coast, or keep running. |

## Operations

### Speed

Runs the motor at a speed.

| Pin | Meaning |
|---|---|
| Speed (input) | in %: -100 (full reverse) to 100 (full forward); 0 to 100 for a motor that runs one way. Unwired: the value set in the block's settings. |

The motor does not jump to the speed: it speeds up and slows down along its ramps, and a change of direction goes through stop (and its pause there). A value past -100..100 is held to it and reported once as a warning.

### Stop

Slows the motor down to stop along its ramp, then brakes or coasts as the motor is set, once on each rising enable.

### Brake

Stops the motor at once, no ramp, shorting it. Once on each rising enable.

### Coast

Stops driving the motor at once, no ramp: it rolls free. Once on each rising enable.

### Read speed

| Pin | Meaning |
|---|---|
| Speed (output) | The speed the motor runs at now, in % (where its ramp is). |

### Read current

| Pin | Meaning |
|---|---|
| Current (output) | The motor's current in mA, from the driver's channel. |

### Read stall

| Pin | Meaning |
|---|---|
| Stalled (output) | 1 when a stall stopped the motor since its speed was last set. |

## Behaviour

- A **setting** (Speed) is sent on the first enabled pass and whenever its value changes; an **action** (Stop, Brake, Coast) once on each rising edge of the enable; a **reading** on every enabled pass (gate it with Every to read less often) and written to the output.
- ENO is on while the motor accepts the calls (a setting or a reading), or pulses on the pass an action was sent. A failed call is reported and ENO goes off.
- A stall (the stall current held for the stall time, when the motor watches it) stops the motor by itself; it is also the motor's STALL event, which an On event block can react to. The next speed runs it again.
