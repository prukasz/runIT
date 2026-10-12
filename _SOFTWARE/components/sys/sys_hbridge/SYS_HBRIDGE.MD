# sys_hbridge — the HBRIDGE contract

Component `components/sys/sys_hbridge/`. A contract, not a module with state: opcodes `0x09xx` (`SYS_DEVICE_CONTRACT_HBRIDGE`) that mean the same on every motor driver (the DRV8962 is the only one). The driver implements the ops it can; chip specifics (current limit, fault latch) are its own `0xFFxx` ops. Port of the legacy `sys_hbridge` (a feature table dispatched by device ID).

| File | Contents |
|---|---|
| `sys_hbridge.h` | The contract table `SYS_HBRIDGE_CONTRACT`, `HBRIDGE_HANDLER` / `HBRIDGE_ENTRY`, the fault reasons, the events table, the wrappers |
| `sys_hbridge.c` | The wrappers (`sys_device_call` of the op, nothing else) |

A channel is a full bridge (a motor, both directions) or a half bridge (a solenoid, one direction), as the driver is configured.

| Op | Args | Answer | Notes |
|---|---|---|---|
| `0x0900` SET_DRIVE | `{u8 channel, i16 drive}` | – | PWM steps (12 bits, `IO_PWM_DUTY_MAX`): -4095 (full reverse) .. 4095 (full forward) for a full bridge, 0..4095 for a half bridge; else `ERR_DEV_RANGE`. A full bridge brakes below 20 (0.5 %) |
| `0x0901` BRAKE | `{u8 channel}` | – | Short the terminals (outputs on, inputs low) |
| `0x0902` COAST | `{u8 channel}` | – | Release the motor (outputs high-Z) |
| `0x0903` GET_CURRENT | `{u8 channel}` | `i32` mA | |

Legacy drive was a `float` -1.0..1.0; the new tree has no floats on the wire: the drive is a count of PWM steps (it was thousandths until 2026-10-12; the PWM is 12-bit, so a thousand steps wasted a quarter of its resolution).

## Events

Per fault, published by the driver: `HBRIDGE_EV_OVERCURRENT` `0x0900`, `HBRIDGE_EV_THERMAL_OR_UVLO` `0x0901`, `HBRIDGE_EV_EXTERNAL` `0x0902`; source = the driver, channel = the channel, value = its current in mA. All three are `SYS_EVENT_NOTIFY`: a fault is something the user must see (the events stream). `sys_hbridge_fault_reason_e` (NONE, OVERCURRENT, THERMAL_OR_UVLO, EXTERNAL) is the reason a driver keeps per channel.

## Wrappers

`sys_hbridge_set_drive(device, channel, drive)`, `_brake`, `_coast`, `_get_current_mA(device, channel, &mA)`.

## Not yet

- The response to a fault is the driver's own (`turn_off_at_ocp` brakes the channel); a system-wide response matrix for bridge faults like the power manager's is not designed.
- The power mux a bridge is fed from is not checked by the driver (the DRV8962 does not look at its `POWER_SUPPLY` device).
