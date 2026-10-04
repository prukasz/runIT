# How to make a device's pins CSV

The pins CSV lists the **ports of the IC** that a wire can usefully reach. Packages do not matter: the same IC has the
same ports in every package, so nothing here depends on one. It lives next to the device's other app files:
`components/devices/device_<chip>/app/device_<chip>.pins.csv`. The columns and what the generator does with them are in
[device-annotations.md](device-annotations.md) (section *Pins and the symbol*).

## 1. Get the pin table

1. Put the datasheet PDF in the device folder (`components/devices/device_<chip>/`).
2. Extract the text, keeping the layout: `pdftotext -layout <chip>.pdf out.txt`, then search it for `Pin Functions`,
   `Pin Descriptions` or `Pin Configuration`. The table is on the first pages (3 to 5).
3. Text extraction scrambles multi-line descriptions: read the pin name, the number and the description from the
   package drawing next to the table when the columns look shifted. Any package of the IC will do.

## 2. Decide which pins become ports

A port is a pin that can **power something, receive power, measure, or make / receive a signal**.

| Keep | Leave out |
|---|---|
| supply in / out (`supply_in`, `supply_out`) | `NC` / no connect |
| ground (`gnd`) | exposed / thermal pads (merged into ground) |
| signal inputs and outputs, GPIOs, enables, resets, alerts, address straps (`io`) | pins that only take a capacitor, resistor or inductor: `DECAP`, `CAP`, `VCP`, `CPH` / `CPL`, `BOOT*`, `SW*`, `COMP`, `FSW`, internal regulator outputs |
| measurement inputs: shunt `IN+` / `IN-`, current sense, ADC channels (`io`, mode `ADC`) | |
| bus lines: I2C `SCL` / `SDA`, USB, Type-C (`bus`) | |

## 3. Merge pins that serve the same function

When the datasheet lists several pins for one function, make **one row** with one name:

- repeated supply or output pins (`VM` x4, `OUT1` x3);
- several grounds, and the thermal pad, when the datasheet says to tie them to the same ground (power ground + signal
  ground become one `GND`);
- keep pins apart when they are different functions that merely share a name pattern (`OUT1` and `OUT2` are two
  half-bridges, so two ports).

The `pin` column is the row's position (1, 2, 3 ... without gaps, in the datasheet's pin order), not a package pin.

## 4. Fill the columns

One row per port. Header (exactly this, in this order):

```
pin,name,kind,dir,modes,default_mode,group,mV_min,mV_max,bind_pin,cfg_field,note
```

- `name`: letters, digits and `_`, starting with a letter (it becomes an SVG id): `3V3` becomes `V3V3`, `IN-1` becomes `IN1_N`, `EN/UVLO` becomes `EN`.
- `kind`: `supply_in`, `supply_out`, `gnd`, `io`, `bus`. `dir`: `in`, `out`, `inout` (seen from the chip).
- `modes`, `default_mode`: for every `io` port, the signal types it can be, `|`-separated, from `BINARY`, `PWM`, `ADC`, `DAC` (a pin that measures a voltage is `ADC`, a logic pin `BINARY`).
- `group`: pins that belong together (`power`, `i2c`, `address`, `alert`, `half_bridge1` ...).
- `mV_min`, `mV_max`: from *Recommended Operating Conditions* for supplies, from *Absolute Maximum* or the input-high limit for signal pins. Leave blank when the datasheet does not say. A user can raise a pin's limit per placed device (a divider in front of an ADC input), so the CSV holds the chip's own value.
- `bind_pin`: only for pins the device's contracts address by number (ADS7128 channels `0..7`, TCA6424A ports `0..23`, an ESP32 GPIO number). It must be a choice of the device's `pin` contract parameter.
- `cfg_field`: the pin ref of the create frame that a wire to this port fills (`intr_pin`, `en_pin` ...). It must match a `sys_io_pin_ref_t` field of the device header.
- `note`: one short line, shown as the pin's tooltip: what it does and any wiring rule ("pull up to VCCI"). No package pin numbers.

## 5. Check and generate

1. The device needs a `device_<chip>.json` in the same `app/` folder with `"symbol": {"label": "<CHIP>"}` (optional
   `variants`: `[{"id", "label", "hidden": [port names]}]` for breakout boards that do not bring out every port).
   Without that json the CSV is not read.
2. Validate: from `data-structures/auto-annotations/device/` run `python -c "import device_symbol, pathlib; print(len(device_symbol.read_ports(pathlib.Path('<path to csv>'))))"`. It prints the port count or the first error (columns, duplicate names, unknown kind or mode, numbering).
3. Generate: `python data-structures/auto-annotations/generate-all.py` writes `ports` into `data-structures/devices/device_<id>.generated.json` and the symbol SVG(s) beside it. `generate-all.py --check` fails when they are stale.
4. Run the app tests (`npm test` in `app/`).

The existing CSVs are the examples: `device_pca9685` (16 outputs, address straps, I2C), `device_drv8962` (merged supply, ground and output pins), `device_ina3221` (measurement pairs), `device_gpio_esp` (a module's GPIOs).
