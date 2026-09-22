# Device auto-annotations

Device annotations are source comments in `components/codecs/decoders/device/dec_device_<name>.h`. They generate one self-contained descriptor at `structures/devices/<device-id>.generated.json`. Generated files are output only; do not edit them.

Generate all annotated devices with:

```powershell
python auto-annotations/device/generate-device-json.py
```

## Header layout

Place the annotations after includes and before the install packet. An annotated header must declare exactly one `packet_sys_device_install_<name>_t` packet.

```c
//@id device_example
//@version 1.0.0
//@title Example device
//@description Short user-facing device description.
//@protocol i2c
//@tags i2c adc monitor
//@contract-provider SYS_DEVICE_CONTRACT_IO
//@capability pins @one_of [0,1,2,3]

//@contract packet_sys_io_get_voltage_t @alias Read channel voltage
//@param pin @alias Channel @one_of [0,1,2,3]
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
| `//@contract-provider <enum>` | no | `contractProvider` | Firmware contract actually exposed by the adapter. |
| `//@capability <name> ...` | no, repeatable | `capabilities[]` | Intrinsic static device capability. |

Only put device-intrinsic values here: channel count, fixed address straps, supported modes, or silicon limits. Board pin assignments, bus topology, and installed IDs belong to the project/board. Runtime-negotiated limits belong to runtime discovery.

## Contracts

`//@contract` must reference a packet struct name, never a decoder function or a `HEADER_` macro. Generation fails for an unknown packet.

```c
//@contract <packet_struct_name> [@alias <action label>]
//@param <packet_field> [@alias <label>] [@type <C type>] [@enum <enum>] [@unit <unit>] [@one_of [value,...]] [@min <value>] [@max <value>] [@default <value>] [@optional]
//@returns <name> [@type <C type>] [@unit <unit>]
//@description <text>
```

A contract is the device-restricted view of a generic system packet. List only operations whose vtable function is supported by the adapter. The generated descriptor embeds the referenced packet definition, including class/header bytes and wire-field layout.

`@one_of` accepts numbers and enum symbols. Numeric choices become JSON numbers; enum symbols remain strings. `@min`, `@max`, and `@default` resolve C defines and `CONFIG_*` symbols where possible; unresolved values become JSON `null` and are reported by the generator.

## Install packet fields

Inline annotations on fields of the packed install packet are the canonical wire metadata:

```c
uint8_t i2c_bus; //@required @alias I2C Bus @one_of [0,1]
uint8_t alert_pin; //@group alert-pin @role pin @sentinel SYS_GPIO_NONE
uint32_t vref_mv; //@required @alias ADC Reference Voltage @unit mV @min 1
```

Supported field annotations: `@required`, `@optional`, `@alias`, `@min`, `@max`, `@one_of`, `@available`, `@default`, `@unit`, `@ref`, `@sentinel`, `@group`, `@role`, and `@note`.

Use field `@alias` for the canonical wire-field label. Use parameter `@alias` only when a generic field has clearer device-specific meaning: generic `pin` becomes `ADC Channel` for ADS7128 or `PWM Channel` for PCA9685. Do not duplicate identical labels in both places.

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
