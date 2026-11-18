# sys_hbridge — the HBRIDGE contract

Component `components/sys/sys_hbridge/`. A contract, not a module with state: opcodes `0x09xx` (`MODULE_ID_HBRIDGE` = `0x09`, the contract's own ID) that mean the same on every motor driver. A driver implements the ops it supports; chip specifics (current limits, fault latching) are that driver's own `0xFFxx` ops.

| File | Contents |
|---|---|
| `sys_hbridge.h` | The contract table `CONTRACT_HBRIDGE_OPERATIONS` (prefix `CONTRACT_HBRIDGE`), arg structs (`hbridge_channel_args_t`, `hbridge_drive_args_t`), `SYS_HBRIDGE_DRIVE_MAX`, fault reasons, and `HBRIDGE_EVENTS` |
| `contract_hbridge.json` | The descriptor of the contract (`templates/`) |
| `CMakeLists.txt` | Header only; requires `sys_device`, `sys_event` |
| `AGENTS.md` | Rules for changing this folder |

A channel is a full bridge (a motor, both directions) or a half bridge (a solenoid, one direction), as the driver is configured.

Routed requests use the device router frame `[0x20][dir u8][id u8][op u16][args]` ([`SYS_DEVICE_ROUTER.MD`](../sys_device_router/SYS_DEVICE_ROUTER.MD), [`SYS_DEVICE.MD`](../sys_device/SYS_DEVICE.MD)).

| Op | Side | Args | Answer | Notes |
|---|---|---|---|---|
| `0x0900` DRIVE | W | `{u8 channel, i16 drive}` | – | PWM steps (12 bits, `SYS_HBRIDGE_DRIVE_MAX` = 4095, `IO_PWM_DUTY_MAX`): `-4095` (full reverse) .. `4095` (full forward) for a full bridge, `0..4095` for a half bridge; else `ERR_DEV_RANGE`. A full bridge brakes below `20` (0.5 %) |
| `0x0901` BRAKE | W | `{u8 channel}` | – | Short the terminals (outputs on, inputs low) |
| `0x0902` COAST | W | `{u8 channel}` | – | Release the motor (outputs off) |
| `0x0903` CURRENT | R | `{u8 channel}` | `i32` mA | Allowed while suspended |

The drive is an integer count of PWM steps. The wire carries no floats: 12-bit integer count gives full resolution over standard 12-bit PWM.

- A channel the driver does not have: `ERR_DEV_BAD_PIN {id, pin}`.
- DRIVE, BRAKE and COAST while suspended: `ERR_DEV_SUSPENDED`.
- A half bridge has no brake band: drive `0` is PWM 0 with the bridge enabled, not a BRAKE.

## Events (`MODULE_ID_HBRIDGE` = `0x09`)

Per fault, published by the driver (`EVT_TAG(0x09, code)`):

| Event | Code (tag) | Flags | Payload |
|---|---|---|---|
| `HBRIDGE_EV_OVERCURRENT` | `0x01` (`0x00090001`) | `SYS_EVENT_NOTIFY` | `i32 current_mA` |
| `HBRIDGE_EV_THERMAL_OR_UVLO` | `0x02` (`0x00090002`) | `SYS_EVENT_NOTIFY` | `i32 current_mA` |
| `HBRIDGE_EV_EXTERNAL` | `0x03` (`0x00090003`) | `SYS_EVENT_NOTIFY` | `i32 current_mA` |

Source = the driver device, channel = the channel index.

`sys_hbridge_fault_reason_e` (`SYS_HBRIDGE_FAULT_NONE = 0`, `SYS_HBRIDGE_FAULT_OVERCURRENT = 1`, `SYS_HBRIDGE_FAULT_THERMAL_OR_UVLO = 2`, `SYS_HBRIDGE_FAULT_EXTERNAL = 3`) tracks the fault reason a driver keeps per channel. Nothing is published while suspended, and a faulted channel stays latched until the driver clears it. System-wide fault policies are outside this contract.

## Calls

Call directly from C through [`sys_device`](../sys_device/SYS_DEVICE.MD) with prefix `CONTRACT_HBRIDGE`:

```c
ERR_TRY(SYS_DEVICE_SET(bridge, CONTRACT_HBRIDGE, OP_DRIVE, .channel = 0, .drive = 800));
ERR_TRY(SYS_DEVICE_SET(bridge, CONTRACT_HBRIDGE, OP_BRAKE, .channel = 0));
ERR_TRY(SYS_DEVICE_SET(bridge, CONTRACT_HBRIDGE, OP_COAST, .channel = 0));
int32_t mA = 0;
ERR_TRY(SYS_DEVICE_GET(bridge, CONTRACT_HBRIDGE, OP_CURRENT, &mA, .channel = 0));
```

## Devices

A motor on a channel is the virtual device [`device_dc_motor`](../../devices/virtual/device_dc_motor/device_dc_motor.c): it drives a channel through `DRIVE`, `BRAKE`, `COAST`, and `CURRENT` with ramps, dead zone, start/top speed, trim, reversing through stop, and stall stop.
