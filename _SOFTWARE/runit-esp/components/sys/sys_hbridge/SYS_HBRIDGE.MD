# sys_hbridge — the HBRIDGE contract

Component `components/sys/sys_hbridge/`. A contract, not a module with state: opcodes `0x09xx` (`SYS_DEVICE_CONTRACT_HBRIDGE`) that mean the same on every motor driver. The driver implements the ops it can; chip specifics (current limit, fault latch) are its own `0xFFxx` ops (the DRV8962's are in `components/devices/device_drv8962/`).

| File | Contents |
|---|---|
| `sys_hbridge.h` | The contract table `SYS_HBRIDGE_CONTRACT`, `HBRIDGE_HANDLER` / `HBRIDGE_ENTRY`, the fault reasons, the events table, the wrappers |
| `sys_hbridge.c` | The wrappers (`sys_device_call` of the op, `sys_device_read` for GET_CURRENT; nothing else) |
| `contract_hbridge.json` | The descriptor of the contract (`templates/`) |
| `AGENTS.md` | Rules for changing this folder |

A channel is a full bridge (a motor, both directions) or a half bridge (a solenoid, one direction), as the driver is configured.

| Op | Args | Answer | Notes |
|---|---|---|---|
| `0x0900` SET_DRIVE | `{u8 channel, i16 drive}` | – | PWM steps (12 bits, `IO_PWM_DUTY_MAX`): -4095 (full reverse) .. 4095 (full forward) for a full bridge, 0..4095 for a half bridge; else `ERR_DEV_RANGE`. A full bridge brakes below 20 (0.5 %) |
| `0x0901` BRAKE | `{u8 channel}` | – | Short the terminals (outputs on, inputs low) |
| `0x0902` COAST | `{u8 channel}` | – | Release the motor (outputs off) |
| `0x0903` GET_CURRENT | `{u8 channel}` | `i32` mA | Allowed while suspended |

The drive is an integer count of PWM steps. The wire carries no floats: the PWM is 12 bits, so a 12-bit count gives the driver its full resolution.

- A channel the driver does not have: `ERR_DEV_BAD_PIN {id, pin}`.
- SET_DRIVE, BRAKE and COAST while suspended: `ERR_DEV_SUSPENDED`.
- A half bridge has no brake band: drive 0 is PWM 0 with the bridge enabled, not a BRAKE.

## Events

Per fault, published by the driver: `HBRIDGE_EV_OVERCURRENT` `0x0900`, `HBRIDGE_EV_THERMAL_OR_UVLO` `0x0901`, `HBRIDGE_EV_EXTERNAL` `0x0902`; source = the driver, channel = the channel, value = its current in mA. All three are `SYS_EVENT_NOTIFY`: a fault is something the user must see (the events stream).

`sys_hbridge_fault_reason_e` (NONE, OVERCURRENT, THERMAL_OR_UVLO, EXTERNAL) is the reason a driver keeps per channel.

## The response to a fault

The driver decides the response. With its `turn_off_at_ocp` option set, an overcurrent brakes the channel; without it the channel keeps driving and only the fault is latched and published. The driver's fault line is edge triggered: the driver reads the channel currents to tell which channel faulted (a channel at its limit, else the one channel that was driving, else a chip-wide shutdown). Nothing is published while the device is suspended.

A system-wide response to a bridge fault (as the power manager gives a supply fault) is not part of this contract. The power mux a bridge is fed from is not checked by the driver: the DRV8962 does not read its `POWER_SUPPLY` device.

## Wrappers

`sys_hbridge_set_drive(device, channel, drive)`, `sys_hbridge_brake(device, channel)`, `sys_hbridge_coast(device, channel)`, `sys_hbridge_get_current_mA(device, channel, &mA)`.

## Devices

| Contract | Implemented by |
|---|---|
| HBRIDGE | DRV8962 (`components/devices/device_drv8962`) |

The VM reaches the contract through the HBRIDGE block (`components/vm/vm_blocks/device-contract/hbridge`, VM_BLOCKS.MD).
