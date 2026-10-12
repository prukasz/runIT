# Device annotations

`generate-devices.py` writes one descriptor per device type to `data-structures/devices/<device id>.generated.json` (and the user guide `<device id>.md` when `device_<chip>.md` sits next to the header): what the app needs to add the device to a board, configure it and use it. The device's own header and `.c` are the only source; every name a user sees is an explicit `@alias` (a field without one is an error). Generated output is not edited.

```powershell
python data-structures/auto-annotations/device/generate-devices.py [--check]
python -m unittest data-structures/auto-annotations/device/test_generate_devices.py
```

The device id is the component's folder name (`device_servo`). The generator reads three places of a device:

| Place | What it gives |
|---|---|
| the **record** in `device_<chip>.h`: `//#device` with `//@<tag> <text>` lines under it | identity, which pins the contracts take, events, values |
| the **create struct** named by `.cfg_size = sizeof(<cfg_t>)` in `SYS_DEVICE_TYPE_DEFINE` (`device_<chip>.c`) | the install form: fields, ranges, pins on other devices |
| the **op table** `static const sys_device_op_t k_ops[]` | which contract ops the device implements, and its own ops |

The type id is `SYS_DEVICE_TYPE_<NAME>` of the define, resolved from `sys_device_types.h`. The CREATE frame is `[0x05][device id u16][op 0x0000][type u16][the create struct]` (contracts catalog, device `CREATE`).

## The record

```c
//#device
//@alias Servo
//@description Hobby servo on a PWM pin of another device: ...
//@protocol pwm
//@tags servo pwm motion
//@pins IO @one-of [0..15] @alias PWM channel
//@param IO_OP_SET_PWM_FREQ.pin @device-wide @note One frequency for all channels
//@event PMON_EV_OCP_CRITICAL PMON_EV_OCP_WARNING @channel Monitor channel
//@value POSITION @when servo_type=$SERVO_TYPE_ANGLE @title Angle @unit deg @range angle_min_deg..angle_max_deg
```

A line without a tag continues the previous one; one record per header.

| Tag | Meaning |
|---|---|
| `@alias`, `@description`, `@protocol` | required: the card's name and text; `protocol` is any of `i2c`, `native`, `pwm` |
| `@tags <words>`, `@datasheet <url>` | search terms; the manufacturer's datasheet |
| `@pins <CONTRACT\|*> @one-of [..] @alias <name> [@note ..]` | the values the `pin` / `channel` field (`@id pin` in the contract) takes on this device, and what it is called here (`ADC channel`, `Bridge channel`); `*` for every contract. Many |
| `@param <OP_SYMBOL>.<field> [@alias ..] [@note ..] [@min ..] [@max ..] [@default ..] [@one-of [..]] [@device-wide]` | narrows one argument of one op: a range, a default, `device-wide` (the field selects nothing on this device: the app hides it and sends 0). The op must be one the device implements. Many |
| `@event <EVENT_SYMBOL...> [@channel <name>]` | the contract events the device publishes (`IO_EV_RISING`, `VREG_EV_OVP`, ...; they must belong to a contract the device implements), and what their channel is called (the `@pins` values of the contract apply). Many |
| `@value NAME @when f=v ... @title <name> [@unit u] [@range a..b]` | what one number the device takes (a position, a level) means by the install settings; the first variant whose `@when` holds applies. `@when` is `<install field>=<number or $SYMBOL>`; a range bound is a number, a `$SYMBOL` or an install field (`angle_min_deg`). A block's input names it with `@spec` |
| `@pwm-frequencies <value> [@count-bits]` | how many different PWM frequencies a per-pin-frequency device runs at once (`CONFIG_..._TIMER_MASK @count-bits` counts the set bits) |

`[a, b, 0x10..0x17, $SYMBOL]` is the list form: numbers, ranges and members of a published enum (`$SYS_IO_MODE_ADC`, a `//#ref-enum` member, enums.json); a value may also be a `#define` or a `CONFIG_*` option.

## The create struct

The packed struct is laid out like a wire struct (contract-annotations.md: sequential offsets, nested structs expanded). Its field tags go in the trailing comment of the field:

| Tag | Meaning |
|---|---|
| `@alias <name>` | required |
| `@description <text>`, `@note <text>` | a tooltip; the longer help behind the field's (i) button |
| `@unit`, `@min`, `@max` | `@min` / `@max` take a number, a `#define` or a `CONFIG_*` option |
| `@default <v>` | what the app starts with: a number, `#define` or `$SYMBOL`; it must be one of `@one-of` / `@choices` when those are given |
| `@one-of [..]` | the permitted values (an `i2c_addr` field has the chip's strap-selectable 7-bit addresses) |
| `@enum-ref <enum>`, `@choices 0=Off,1=On` | the value is one of an enum, or of a few literals (`_` is a space inside a name) |
| `@required` | a pin on another device that must be wired (otherwise `SYS_IO_PIN_NONE` = not connected is fine) |
| `@modes [..]`, `@default-mode $X` | on a `sys_io_pin_t` field (a pin on another device): the pin modes that make sense for this use (the app also intersects them with what the provider supports) and the mode a new device starts with (one of `@modes`) |
| `@aliases a\|b\|c` | an **array** of pins: one name per element (`IN1\|IN2\|...`; a single entry names every element). Required for an array of pins. `@modes` / `@default-mode` of an array are lists per element in the same form (`[$X]*4\|[$Y]`; `*n` repeats an entry) |

A `sys_io_pin_t` field expands to `device` (a device id) and `pin` (an index of that device) and is published as a pin reference (`pin_ref: true`). A device with `typedef sys_device_none_t <cfg>;` takes no create struct (`fields: []`).

## The op table

```c
static const sys_device_op_t k_ops[] = {
    DEVICE_ENTRY(RESET), DEVICE_ENTRY(SUSPEND), DEVICE_ENTRY(RESUME),
    IO_ENTRY(SET_LEVEL),                  // a contract op: its alias, id and arguments come from the catalog
    SYS_PINS_STATE_ENTRIES,               // an `*_ENTRIES` macro stands for the rows it expands to
    //@op SERVO_OP_SET @alias Move @description Moves the servo to a position
    {SERVO_OP_SET, set_h},                // an own op: annotated like a router op
    //@internal
    {TPS_OP_RISE_STEP, rise_step_h},      // not for the app
};
```

- `X_ENTRY(NAME)` rows name a contract op (`#define X_ENTRY(name) {X_OP_##name, ...}`); the descriptor lists them per contract, with the device's `@pins` / `@param` narrowing. `EVENT_ENTRY(ON_EVENT)` (the listener hook) is not published.
- **Own ops** (`SYS_DEVICE_OWN_OP(n)`, ids `0xFFnn`) need `//@op <SYMBOL> @alias <name> @description <text> [@returns ...]` above their row. Their arguments are the struct of `SYS_DEVICE_HANDLER(handler, args_t)` (annotated as a wire struct); `@returns <type> @returns-alias <name> [@unit u] [@choices ..]` or `@returns <struct>` and `@returns-tail` as in contract-annotations.md. `//@internal` above a row hides it.

## Output

`id`, `alias`, `description`, `protocols`, `tags`, `datasheet`, `source_file`, `guide`, `type {symbol, id}`, `create {frame, struct, size, fields[]}` (a field: `offset`, `size`, `type`, `alias`, tags, `one_of`, `default`, `pin_ref`, `modes`, `aliases`, nested `fields`), `contracts[] {contract, id, alias, ops[] {name, symbol, id, alias, description, returns, params[]}}`, `own_ops[]`, `events[] {events[], channel}`, `values`, `pwm_frequencies`.

## Not here yet

Ports and the generic IC symbol (`device_<chip>.pins.csv`, `device_symbol*.py`, `pins-csv-guide.md`), the generated limits header and the canvas layouts of the legacy generator are not wired to this generator. `device_symbol_svg.py` still needs its non-ASCII separator fixed before it can be reused.
