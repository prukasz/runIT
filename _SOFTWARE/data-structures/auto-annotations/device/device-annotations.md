# Device auto-annotations

Device annotations are source comments in `components/codecs/decoders/device/dec_device_<name>.h`. They generate one self-contained descriptor at `data-structures/devices/<device-id>.generated.json`. Generated files are output only; do not edit them.

Generate all annotated devices with:

```powershell
python data-structures/auto-annotations/device/generate-devices.py components/codecs/decoders data-structures/devices
```

The first argument is the decoders root to scan, the second is the output directory - each device's output filename is generated automatically from its own `//@id` (`<device-id>.generated.json`), never passed explicitly. `$SYMBOL` resolution pulls live from `data-structures/auto-annotations/enums/generate-enums.py`'s scan (see that script's own doc comment) - no separate enums JSON needs to be generated or committed first.

## Keyword reference by case

| Case | Keywords |
|---|---|
| Device metadata (top-level, before first `//@contract`) | `id`, `version`, `title`, `description`, `protocol`, `tags`, `contract-provider` |
| `//@self-property NAME` | `one-of` |
| `//@property NAME` | `ref`, `one-of` |
| `//@contract <packet>` | `alias` |
| `//@param <field>` | `arg`, `alias`, `type`, `unit`, `one-of`, `available`, `min`, `max`, `default`, `optional` |
| `//@returns <name>` | `type`, `unit` |
| `//@description` (after `@contract`) | *(free text, no sub-keywords)* |
| Install struct field (`// ...` on a packed field) | `required`, `optional`, `alias`, `min`, `max`, `one-of`, `available`, `default`, `unit`, `ref`, `sentinel`, `group`, `role`, `note` |
| `//#ref-enum` marker | `alias` |
| Enum member (`// ...` on an enum value) | `alias`, `description` |
| Anywhere a value is expected | `$SYMBOL` prefix - resolve against a `//#ref-enum` enum, hard error if unresolved |
| Generated only, never authored | `instance` (`true` on a synthesized `device_id` parameter - see Contracts) |

## Header layout

Place the annotations after includes and before the install packet. An annotated header must declare exactly one `packet_sys_device_install_<name>_t` packet.

```c
//@id device_example
//@version 1.0.0
//@title Example device
//@description Short user-facing device description.
//@protocol i2c
//@tags i2c adc monitor
//@contract-provider $SYS_DEVICE_CONTRACT_IO
//@self-property PIN @one-of [0,1,2,3]
//@property PIN-MODE @ref sys_io_mode_e @one-of [$SYS_IO_MODE_ADC]

//@contract packet_sys_io_get_voltage_t @alias Read channel voltage
//@param pin @arg PIN @alias Channel
//@returns voltage_mV @type uint32_t @unit mV
//@description Read the selected channel.
```

## Device metadata

| Annotation | Required | Generated field | Rule |
|---|---:|---|---|
| `//@id <id>` | yes | `id` | Stable, unique device identifier. |
| `//@version <semver>` | yes | `version` | Descriptor schema version for this device, not firmware build version. |
| `//@title <text>` | yes | `title` | Short UI name. |
| `//@description <text>` | yes | `description` | Concise user-facing purpose and behavior. |
| `//@protocol <items>` | no | `protocols` | Whitespace-separated transports, for example `i2c spi`. |
| `//@tags <items>` | no | `tags` | Whitespace-separated search terms. |
| `//@contract-provider $<symbol>` | no | `contractProvider` | Firmware contract actually exposed by the adapter. The `$` marks it as a global symbol - it must resolve against a `//#ref-enum` enum (see below) or generation fails. |
| `//@self-property NAME @one-of [...]` | no, repeatable | (lookup table only, not emitted directly) | A device-local value domain with no meaning outside this device - plain literals, nothing to resolve (e.g. channel indices, address straps). |
| `//@property NAME [@ref <enum>] @one-of [$SYMBOL, ...]` | no, repeatable | (lookup table only, not emitted directly) | A value domain whose members are real global symbols. Each `$SYMBOL` must resolve against a `//#ref-enum` enum or generation fails. `@ref` is optional documentation - the owning enum is inferred from which `//#ref-enum` enum actually contains the symbol. |

Only put device-intrinsic values here: channel count, fixed address straps, supported modes, or silicon limits. Board pin assignments, bus topology, and installed IDs belong to the project/board. Runtime-negotiated limits belong to runtime discovery.

`@self-property`/`@property` exist to be referenced from a `@param` via `@arg NAME` (see below) so the same value domain isn't retyped on every contract that takes it. Don't define one that nothing references.

## Referencing global enums (`$SYMBOL` / `//#ref-enum`)

A `$`-prefixed token (in a `@property @one-of` list, `@contract-provider`, or a field's own `@one-of`/`@sentinel`) means "resolve this against a real enum member, don't treat it as a string." It only resolves against enums explicitly opted in at their C definition site:

```c
//#ref-enum @alias IO Mode
typedef enum sys_io_mode_e {
  SYS_IO_MODE_ADC = 7, //@alias ADC @description Measures the exact voltage on the pin instead of just on/off - use to read sensors that report a varying value
  ...
} sys_io_mode_e;
```

- `//#ref-enum` goes on the line immediately before the `typedef enum`. Only marked enums are scanned - this is a deliberate opt-in, not a blanket project-wide scan, so an internal/private enum never accidentally becomes public wire vocabulary.
- The marker's own `@alias` is the enum's display title (e.g. "IO Mode").
- Each member's trailing `// @alias <label> @description <text>` is optional. No comment (or an empty one) means no alias - a client falls back to formatting the raw symbol name. `@description` should be written for someone who doesn't know the underlying electrical/firmware concept - explain what the option is *for*, with a concrete use case, not just what it technically does.
- An unresolved `$SYMBOL` (not found in any `//#ref-enum` enum) is a hard generation error, never a silent fallback to a bare string - that silent-fallback behavior was the old design's actual bug.
- A member name defined in two different `//#ref-enum` enums is also a hard error (ambiguous).

## Contracts

`//@contract` must reference a packet struct name, never a decoder function or a `HEADER_` macro. Generation fails for an unknown packet.

```c
//@contract <packet_struct_name> [@alias <action label>]
//@param <packet_field> [@arg <PROPERTY_NAME>] [@alias <label>] [@type <C type>] [@unit <unit>] [@one-of [value,...]] [@min <value>] [@max <value>] [@default <value>] [@optional]
//@returns <name> [@type <C type>] [@unit <unit>]
//@description <text>
```

A contract is the device-restricted view of a generic system packet. List only operations whose vtable function is supported by the adapter. The generated descriptor embeds the referenced packet definition, including class/header bytes and wire-field layout.

**Completeness rule:** every field the referenced packet marks `@required` (in the generic decoder, e.g. `dec_sys_contracts.h`) must appear in the contract's own `@param` list. Generation fails otherwise - a device that silently omits a required generic field (like `route_mask` on `packet_sys_io_configure_intr_t`) produces a client-facing form that can't actually build a valid wire packet.

**`device_id` is synthesized automatically**, never annotated: it means "which installed device instance this call addresses," supplied by the calling context (the device the client is already configuring), not a value a user picks per contract. The generator injects it into every contract's `parameters` as `{"name": "device_id", "alias": ..., "instance": true}` - a client renders anything with `"instance": true` as auto-filled context, never as a form field, and never needs its own `@param device_id` line in any header.

`@arg <NAME>` pulls a param's constraint (its `@one-of` list, and `@ref` if the property has one) from a `@self-property`/`@property` defined earlier in the same header, instead of retyping it. A param may still use its own inline `@one-of`/`@min`/`@max` when the value domain isn't shared with anything else.

`@one-of` accepts numbers and `$`-prefixed enum symbols (which resolve per the rules above). `@min`, `@max`, and `@default` resolve C defines and `CONFIG_*` symbols where possible; unresolved non-`$` values become JSON `null` and are reported by the generator - only `$`-prefixed values are hard errors on failure to resolve.

## Install packet fields

Inline annotations on fields of the packed install packet are the canonical wire metadata:

```c
uint8_t i2c_bus; //@required @alias I2C Bus @one-of [0,1]
uint8_t alert_pin; //@group alert-pin @role pin @sentinel SYS_GPIO_NONE
uint32_t vref_mv; //@required @alias ADC Reference Voltage @unit mV @min 1
```

Supported field annotations: `@required`, `@optional`, `@alias`, `@min`, `@max`, `@one-of`, `@available`, `@default`, `@unit`, `@ref`, `@sentinel`, `@group`, `@role`, and `@note`. Install-packet fields don't support `@arg` - they're always inline, since the install struct is unique per device and its fields aren't shared across contracts the way a `pin` parameter is.

Use field `@alias` for the canonical wire-field label. Use parameter `@alias` only when a generic field has clearer device-specific meaning: generic `pin` becomes `ADC Channel` for ADS7128 or `PWM Channel` for PCA9685. Do not duplicate identical labels in both places.

A hardware characteristic that isn't a value-domain (e.g. "this pin is active-low") belongs in that field's own `@note`, not in a property - see `intr_pin_pin`/`crit_pin_pin` in the ADS7128/INA3221 headers for the pattern.

## Generated JSON shape

```json
{
  "$schema": "runit://schemas/device-definition/v1",
  "schemaVersion": 1,
  "kind": "device-definition",
  "id": "device_example",
  "version": "1.0.0",
  "install": { "packet": "packet_sys_device_install_example_t", "packet_definition": {} },
  "contracts": [
    { "packet": "packet_sys_io_get_voltage_t", "alias": "Read channel voltage", "parameters": [], "packet_definition": {} }
  ]
}
```

`packet_definition` is generated directly from C and drives little-endian command packing. Firmware remains the final validation authority.

## Current scope

The generator currently supports device identity, capabilities, installation fields, contract restrictions, aliases, parameter constraints, and descriptions. Assets, errors, layout metadata, and runtime capability queries are future directives; document and test any new directive before using it.
