# INA3221 power monitor

The **INA3221** is a Texas Instruments chip that watches three power rails at once. For each rail it reads the
voltage and, through a small shunt resistor, the current. It raises alerts on its own pins when a current goes over a limit.

![Example breakout board with the INA3221: channels CH1 to CH3, the A0 table, the header and the LEDs](ina3221-devboard.jpg)

*The picture is an example breakout board. Yours may differ in its labels and parts; the chip is the same.*

## How the chip measures

- **Shunt voltage:** each channel measures the voltage across its shunt, in steps of **40 µV**, full scale **±163.84 mV**.
- **Bus voltage:** each channel measures the rail's voltage, in steps of **8 mV**, from **0 to 26 V**.
- **Current is not read directly.** The chip has no current register. The app computes it from the shunt voltage:
  **I = V(shunt) / R(shunt)**, and the power is **P = V(bus) × I**.

So the range and the resolution depend on the shunt resistor R in the rail:

| Shunt R | Full scale per channel (±) | One step of current |
|---|---|---|
| 0.1 Ω (100 mΩ) | 1.638 A | 0.4 mA |
| 0.01 Ω (10 mΩ) | 16.38 A | 4 mA |

Full scale is 0.16384 V / R; one step is 0.00004 V / R. A current above full scale does not give a larger number: the reading stops at the top.

## Wiring

- **Power:** the chip is supplied on **VS** and **GND**.
- **Each channel** goes in series with its load: the shunt sits in the rail, between the supply and the load.
- **Reversed shunt:** a shunt wired backwards reads a **negative** current. Tick its channel as *reversed* in the app
  instead of rewiring it.

## I2C address

The chip's **A0** pin selects the address. Tie A0 to one of four pins:

| A0 tied to | Address (7-bit) |
|---|---|
| GND | 0x40 (default on most boards) |
| VS | 0x41 |
| SDA | 0x42 |
| SCL | 0x43 |

Two chips on one bus need different addresses.

## Alert pins

The chip has four open-drain outputs. They are active low and need a pull-up resistor to a supply.

- **CRI** (critical) and **WAR** (warning): trip when a channel's current crosses its limit. The app uses these two.
- **PV** (power valid) and **TC** (timing control): chip functions the app does not use.

In the app, **Set current alert** arms a limit on a channel. When the pin goes active, the board publishes an event. The
alert pin needs an **interrupt** set on the device before it reacts. A reversed channel can use one alert pin, and each
pin watches one reversed channel.

## Set it up in the app

1. **Add device**, then *Power monitor*.
2. **Settings:** the I2C address, the alert pins you wired, the reversed channels.
3. **Write** in the toolbar sends the settings and start commands to the board.
4. **Test** in the right panel: **Read voltage**, **Read current**, **Read power**, one channel at a time.

## Shunt resistance

Set **Shunt resistance** when you add the device, in milliohms, to the value of the shunts on your board:

- **10 mΩ:** the runIT board's shunts (the default).
- **100 mΩ:** a 0.1 Ω shunt, marked **R100**, as on the example breakout.

Every current and power reads in proportion to this value. A wrong value scales all of them by the ratio of the right
value to the set one. Read the marking on your shunts before you set it.
