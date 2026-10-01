# Device auto-annotations

Device annotations are source comments in the device's own header, `components/devices/device_<name>/include/device_<name>.h` (a device whose create frame is its cfg struct: PCA9685, servo), or in `components/codecs/decoders/device/dec_device_<name>.h` (devices that still have a decoder). They generate one self-contained descriptor at `data-structures/devices/<device-id>.generated.json`. Generated files are output only; do not edit them.

```powershell
python data-structures/auto-annotations/device/generate-devices.py components/codecs/decoders data-structures/devices
python -m unittest data-structures/auto-annotations/device/test_generate_devices.py
```

The first argument is the decoders root to scan (the device headers are found next to it, in `components/devices`), the second the output directory; each file is named from its `//#device` ID. `$SYMBOL` resolution runs `data-structures/auto-annotations/enums/generate-enums.py`'s scan live, so no enums JSON has to be generated first.

**Start from [device-template.txt](device-template.txt)**: a complete header using every tag, with a reference of what the app does with each one (`test_generate_devices.py` keeps it generating).

**Only write what the app reads.** Every record kind accepts a fixed set of tags and generation fails on any other, so a typo or a tag nothing uses never lands silently. When a value is already on the generic packet (alias, unit, type, optional, note of `dec_sys_contracts.h`), don't repeat it on the device.

## JSON form: `device_<chip>.json` (pilot: PCA9685)

A device may describe itself in a JSON file next to its `.c` (`components/devices/device_<chip>/device_<chip>.json`) instead of header comments. The header then keeps only C: `<CHIP>_TYPE_ID`, the packed cfg struct (no `//` annotations) and the class. A header must not have both forms (the generator refuses). The output is the same `device_<id>.generated.json`.

Keys of the file (any other key fails generation):

| Key | Meaning |
|---|---|
| `id`, `title`, `description`, `protocols`, `tags`, `datasheet`, `type_id`, `contract_provider`, `pwm_frequencies`, `count_bits` | The `//#device` tags; `protocols` / `tags` are lists, `contract_provider` has no `$`, `type_id` is the `#define` name or a number |
| `limits` | `NAME: {value, comment}` (or just a number). The generator writes `include/device_<chip>_limits.generated.h` with `#define <CHIP>_<NAME> <value>`; the device's `.c` includes it and range-checks with the same macros, so a limit is written once. `generate-all.py --check` covers this header too |
| `properties` | `NAME: {one_of, alias, note, default, enum_ref}` - the `//#self-property` / `//#property` records |
| `fields` | `<cfg field>: {tags}` - the tags that used to follow the field's `;` (`alias`, `note`, `one_of`, `min`, `max`, `default`, `modes` ...). A pin ref is one entry under its field name (`oe_pin`). A name that is no field of the cfg struct fails generation |
| `contracts` | `[{packet, alias, description, returns, params: {<field>: {tags}}}]` - the `//#contract` records; `params` keep their order |

In any tag value: a list becomes `[a, b]`, `true` is a flag (`"device_wide": true`), and `"limit:NAME"` is the number of that entry of `limits`. Text, notes and limits are the only thing that moves: type id, struct and class stay in C.

Example: [device_pca9685.json](../../../components/devices/device_pca9685/device_pca9685.json).

### Pins and the symbol: `device_<chip>.pins.csv`

A chip's package pins, copied from the datasheet's pin table (one row per pin), make the device's **ports** and a generic IC symbol. Needs `"symbol": {"label", "package"}` in the JSON. The symbol is always the same dual-row package (pins 1..N/2 down the left, the rest up the right, pin 1 top left), so all chips look alike and only the pins differ. Each port is its own clickable, wire-able element on the device canvas.

| Column | Meaning |
|---|---|
| `pin`, `name` | Package pin number (1..N, no gaps) and symbol; `name` becomes the SVG id `pin-<name>` |
| `kind` | `supply_in`, `supply_out`, `gnd`, `io`, `bus` |
| `dir` | `in`, `out`, `inout` |
| `modes`, `default_mode` | `\|`-separated signal types the pin can be (`BINARY`, `PWM`, `ADC`, `DAC`); required for `io` |
| `group` | Pins that belong together (`i2c`, `address`, `led`, `power` ...); a class on the symbol |
| `mV_min`, `mV_max` | Allowed voltage on the pin (supply range, input tolerance) |
| `bind_pin` | The device's own pin number (the `pin` of its contracts) that this port drives; checked against the contracts' `pin` choices |
| `cfg_field` | Pin ref of the create frame that a wire to this port fills (`oe_pin`); checked against the cfg |
| `note` | Shown as the pin's tooltip and as a hint |

Output: `ports` and `symbol` in `device_<id>.generated.json`, and `device_<id>.symbol.generated.svg` beside it. In the SVG every port is `<g id="pin-NAME" class="port kind-… dir-… group-…" data-port data-pin data-kind data-dir data-modes>` with a terminal `<circle id="pin-NAME-t">` where a wire attaches; colours are CSS variables (`--sym-body`, `--sym-line`, `--sym-text`, `--sym-muted`, `--sym-hot`) so the app can theme it, and a `selected` class lights a terminal. Example: [device_pca9685.pins.csv](../../../components/devices/device_pca9685/device_pca9685.pins.csv).

### User guide: `device_<chip>.md`

An optional Markdown file next to the .c is the device's user guide (the app's *User guide* tab renders it as GitHub-flavoured Markdown). The generator copies it to `app/docs/devices/<device id>.md` with a "generated" mark on top, so the files in `app/docs/` are output only. Images go in `images/` next to the guide and are linked `images/<name>` (the generator copies them to `app/docs/devices/images/<device id>/` and fixes the link). The text is the author's own: it is a plain copy and nothing checks it against the C or the JSON (supply voltage, typical use, wiring tips). Devices without a guide in their folder keep a hand-written `app/docs/devices/<id>.md` (no mark). Example: [device_pca9685.md](../../../components/devices/device_pca9685/device_pca9685.md).

## Layout (header comments)

```c
//#device device_example                 <- a record starts: //#<kind> <name> [@tag ...]
//  @title       Example device           <- continuation: comment + two or more spaces
//  @description First line of the text
//               and its second line.      <- a line with no tag continues the tag above
//  @contract-provider $SYS_DEVICE_CONTRACT_IO

// A plain comment (one space) ends the record; it is for developers and never read.
```

- A record runs until the first line that is not `//` followed by two or more spaces.
- Several tags may share a line (`@one-of [0..7] @alias ADC Channel`).
- Free text only follows `@description` and `@note`. Both are client-facing: say what the thing is for and how to use it, never how the firmware implements it. Implementation remarks go in plain `//` comments.
- The old one-line directives (`//@id`, `//@contract`, `//@param` …) fail generation with a pointer here.

## Keyword reference

| Where | Tags |
|---|---|
| `//#device <id>` | `title` (required), `description` (required), `protocol`, `tags`, `datasheet`, `contract-provider`, `pwm-frequencies` (+ `count-bits`) |
| `//#self-property NAME` | `one-of`, `alias`, `note` |
| `//#property NAME` | `enum-ref`, `one-of`, `default`, `alias`, `note` |
| `//#contract <packet>` | `alias`, `description`, `returns`, and `@param` lines |
| `@param <field>` (one per line, in a contract) | `arg`, `alias`, `type`, `unit`, `one-of`, `min`, `max`, `default`, `device-wide`, `note` |
| Install struct field (`//` after the `;`) | `optional`, `alias`, `min`, `max`, `one-of`, `default`, `unit`, `enum-ref`, `note`; rare: `sentinel`, `group`, `encoding`, `terminator` |
| `sys_io_pin_ref_t` install field | `alias`, `note`, `modes`, `default-mode` |
| Any value | `$SYMBOL` (a `//#ref-enum` member), `CONFIG_*` / `#define` for `min` / `max` / `default`, `a..b` ranges in lists |

## Device record

| Tag | JSON | Rule |
|---|---|---|
| `//#device <id>` | `id` | Stable, unique device identifier; names the output file. |
| `@title` | `title` | Short UI name. |
| `@description` | `description` | What the device is and does, for the user. |
| `@protocol` | `protocols` | Whitespace-separated transports (`i2c`, `native`); shown on the device card, `native` picks the tile icon. |
| `@tags` | `tags` | Whitespace-separated search terms; palette search and tag chips. |
| `@datasheet` | `datasheet` | Manufacturer datasheet URL; the device page links it. |
| `@contract-provider $<symbol>` | `contractProvider` | The `sys_device_contract_type_e` the adapter exposes; must resolve. |
| `@type-id <value>` | (install `packet_header`) | Only in a device's own header: the byte after `0x00` in its create frame (`[0x00][type-id][cfg]`, class `0x01`). The value resolves like `@min` (use the `#define` the firmware registers the type with). |
| `@pwm-frequencies <value> [@count-bits]` | `pwm_frequencies` | How many different PWM frequencies a per-pin-frequency device runs at once. The value resolves like `@min`; `@count-bits` counts the set bits of a mask (ESP GPIO: `CONFIG_DEVICE_GPIO_ESP_PWM_TIMER_MASK`). The app warns when a project needs more. |

There is no `@version`: nothing reads a descriptor version.

## Value sets: `//#self-property` and `//#property`

A value set is written once and used by parameters through `@arg NAME`, so the same list, label and note aren't retyped on every contract.

- `//#self-property NAME` holds device-local literals with no meaning elsewhere (channel indices).
- `//#property NAME` holds `$SYMBOL`s of one published enum (`@enum-ref` is inferred when all symbols come from one enum). `@default` must be one of its choices; use it only when the device or its driver establishes that value without a setup command (the app shows it as the effective state and leaves the stored setup empty).
- `@alias` and `@note` on a value set are inherited by every `@param ... @arg NAME`; a parameter's own `@alias` / `@note` wins.
- Define one only when a parameter uses it. Board pin assignments, bus topology and installed IDs belong to the project or the board, not here.

```c
//#self-property PIN
//  @one-of   [0..7]
//  @alias    ADC Channel

//#property PIN-MODE
//  @enum-ref sys_io_mode_e
//  @one-of   [$SYS_IO_MODE_ADC]
//  @default  $SYS_IO_MODE_ADC
```

## Referencing global enums (`$SYMBOL` / `//#ref-enum`)

A `$`-prefixed token means "resolve against a real enum member". It only resolves against enums opted in at their definition:

```c
//#ref-enum @alias IO Mode
typedef enum sys_io_mode_e {
  SYS_IO_MODE_ADC = 7, //@alias ADC @description Measures the exact voltage on the pin instead of just on/off - use to read sensors that report a varying value
  ...
} sys_io_mode_e;
```

- `//#ref-enum` goes on the line before the `typedef enum`; only marked enums are scanned.
- The marker's `@alias` is the enum's display title; each member's `@alias` / `@description` is optional. Write `@description` for someone who doesn't know the electrical or firmware concept: what the option is for, with a use case.
- An unresolved `$SYMBOL`, or a member name defined in two published enums, fails generation.

## Contracts

```c
//#contract packet_sys_io_configure_intr_t
//  @alias       Configure alert
//  @description Configure the on-chip ADC window comparator.
//  @param pin                @arg PIN
//  @param mode               @arg INTR-MODE
//  @param adc_thresh_up_mV   @alias Upper Threshold
```

A contract is the device's view of a generic system packet. List only operations the adapter supports. The name must be a packet struct (not a decoder or `HEADER_` macro); an unknown packet fails generation. The descriptor embeds the packet definition (class and header bytes, wire fields).

- **Completeness:** every field the generic packet marks `@required` must have a `@param` line, even a bare one (`@param voltage_mV`). Optional generic fields appear in the app without one.
- **`device_id` is synthesized**, never annotated: the generator adds `{"name": "device_id", "instance": true}`, which the app fills from the device.
- **Add a tag only when it changes something.** The app merges each parameter with its generic field, so `@alias`, `@unit`, `@note` equal to the generic field's are noise; whether a field is optional always comes from the generic packet. `@type bool` is still needed where the generic field is a `uint8_t` on/off (`level`, `state`): the app shows a switch.
- `@min` / `@max` / `@default` resolve numbers, `$SYMBOL`s, C defines and `CONFIG_*`; an unresolved value fails generation.
- `@device-wide`: the field selects nothing on this device (PCA9685 has one PWM frequency for all channels, so `set_pwm_frequency`'s `pin`). JSON `device_wide: true`; the app hides the field and sends 0.
- `@note`: shown behind the field's (i) button. Falls back to the `@arg` property's note, then to the generic field's.
- `@returns <name>`: the name of the value an OK answer carries. Its type and unit come from the response packet, so nothing else goes on the line.

## Install packet

Each annotated header declares exactly one install packet: `packet_sys_device_install_<name>_t` in a decoder header, or in a device's own header the `typedef struct __packed { ... } d_<name>_cfg_t;` (the generator names its packet `packet_sys_device_install_<name>_t` and `decoder` `d_<name>_create()`). Field annotations go after the `;`. A comment-only line of `//` plus two or more spaces right under a field continues that field's annotation (for a long `@note`).

```c
typedef struct __packed {
  uint8_t device_id;       //@max CONFIG_SYS_DEVICE_MAX_ID
  uint8_t i2c_bus;
  uint8_t i2c_addr;        //@alias I2C Address @one-of [0x10..0x17] @note The resistor on the ADDR pin selects the address at power-up.
  sys_io_pin_ref_t intr_pin; //@alias ALERT @modes [$SYS_IO_MODE_INPUT, $SYS_IO_MODE_INPUT_PULLUP] @default-mode $SYS_IO_MODE_INPUT_PULLUP
                           //  @note Open-drain, active-low.
  uint32_t vref_mV;        //@alias ADC Reference Voltage @unit mV @min 1
} packet_sys_device_install_ads7128_t;
```

- **What the app reads:** `device_id`'s `@max` (highest free ID it hands out); `i2c_bus` takes nothing (the app sets the board's user bus); every other field is a form control, so give it `@alias`, `@unit`, `@min` / `@max` or `@one-of`, and `@note` when the user needs to know something.
- Fields are required unless `@optional`, so `@required` isn't written.
- An `i2c_addr` field must have a nonempty `@one-of` with the chip's strap-selectable 7-bit addresses; the app shows them in hex and flags loaded values outside the list.
- `@default` may be a `$SYMBOL`; it must be one of the field's `@one-of` when both are given. The app uses it when creating a device.

### Pins on other devices: `sys_io_pin_ref_t`

A pin the device uses on another device (interrupt, reset, enable) is one `sys_io_pin_ref_t <name>_pin` field (`sys_io.h`: provider device ID, pin, mode; three bytes). The device's cfg is the create frame, so the field is used as it is (the device copies it into its own state); there is no separate wire type.

The generator expands it to the wire fields `<name>_pin_device_id`, `<name>_pin_pin` (sentinel `SYS_GPIO_NONE` = not connected) and `<name>_pin_mode` (`sys_io_mode_e`), and one pin group keyed `<name>_pin`. That key is also how `board.generated.json` names the pins the board's own devices take, so board devices show the same label.

| Tag | Effect |
|---|---|
| `@alias` | What the pin is for, as the app names it ("ALERT" -> "ALERT pin"). Without it the app derives a name from the field (`intr_pin` -> "Interrupt"), so only write it when that name is wrong for the chip. |
| `@note` | Shown behind the pin block's (i) button (active-low, open-drain …). |
| `@modes [$...]` | The pin modes that make sense for this use; the app also intersects them with what the chosen provider supports. Without it, every `sys_io_mode_e`. |
| `@default-mode $X` | The mode a new device starts with; must be one of `@modes`. |

## Generated JSON shape

```json
{
  "$schema": "data-structures/schema/device-definition.schema.json",
  "schemaVersion": 1,
  "kind": "device-definition",
  "id": "device_example",
  "title": "Example device",
  "install": { "packet": "packet_sys_device_install_example_t", "packet_definition": { "groups": { "intr_pin": { "fields": ["..."], "sentinel_field": "intr_pin_pin", "alias": "ALERT", "note": "..." } } } },
  "contracts": [
    { "packet": "packet_sys_io_get_voltage_t", "alias": "Read channel voltage", "returns": "voltage_mV", "parameters": [], "packet_definition": {} }
  ]
}
```

Every file is validated against `data-structures/schema/device-definition.schema.json` before it is written. `packet_definition` is generated from C and drives little-endian packing; firmware remains the final validation authority.

## Adding a tag

Add a tag only together with the app code that reads it: the tag set of its record in `generate-devices.py`, the schema, a case in `test_generate_devices.py`, [device-template.txt](device-template.txt) (the test fails until the template uses it), and this page.
