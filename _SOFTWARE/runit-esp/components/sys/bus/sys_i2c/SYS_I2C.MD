# sys_i2c — the I2C contract and its client

Component `components/sys/bus/sys_i2c/`. A bus is a device: the ESP32-S3's own controller is [`device_esp32s3_i2c`](../../devices/soc/device_esp32s3_i2c/device_esp32s3_i2c.c), and a mux channel, a bit-banged bus or a simulated one is a device of its own. A project creates as many as it needs, each with its device ID. This component holds the **contract** every bus implements (I2C, `0x02`) and the **client** a chip's driver uses to reach its bus. It makes no bus.

## Files

| File | Role |
|---|---|
| `sys_i2c.h` | The contract table (`CONTRACT_I2C_OPERATIONS`), its frames, the client (`sys_i2c_dev_t`, `sys_i2c_dev_attach`, transfers, register helpers) |
| `sys_i2c.c` | The client; the error table (`ERR_MODULE_DEFINE`) |
| `errors/sys_i2c_errors.h`, `errors/errors_sys_i2c.json` | `ERR_SYS_I2C_MAP` (`MODULE_ID_I2C`) and the messages |
| `CMakeLists.txt` | Requires `errors`, `cmd`, `sys_device` |
| `AGENTS.md` | Rules for changing this folder |

## The contract

| Op | Sides | Flags | Does |
|---|---|---|---|
| `ATTACH` | W | ALWAYS, LOCAL | claims a 7-bit address for the calling device and probes it |
| `DETACH` | W | ALWAYS, LOCAL | lets the address go |
| `XFER` | W | ALWAYS, LOCAL | one transfer to an attached address (the args are pointers: `i2c_xfer_t`) |
| `PROBE` | R | | whether a chip acks an address (1) or not (0) |
| `SCAN` | R | NOLOCK | every address 0x08..0x77 that acks (`i2c_scan_t`, a bitmap) |
| `RECOVER` | W | | clocks a stuck bus free |
| `RAW` | R, W | TAIL | a register of an address no chip is attached at (the app's diagnostics); the bus attaches the address for the one transfer, so it fails on a chip a driver owns |

- ATTACH, DETACH and XFER are ALWAYS (a bus has no outputs to make safe: a suspended bus keeps carrying the transfers of its chips' own suspend hooks) and LOCAL (only a driver's C reaches them, never a command or a VM program).
- SCAN is NOLOCK: its 112 probes, each up to the bus's probe timeout, would hold the device's lock longer than the registry's lock timeout and keep the chips' transfers out. Without the lock each probe takes only the bus (the bus implementation's own lock), the chips' transfers go in between, and REMOVE waits for a SCAN in flight. A bus writes SCAN so that it reads nothing an op under the lock changes (the ESP32-S3's: only the controller's handle, which lives until REMOVE).
- An attached address has an **owner**: the device whose op called ATTACH (`sys_device_caller()`). Only it runs XFER and DETACH on the address (`ERR_I2C_NOT_OWNER{owner}` for anyone else); a second ATTACH is `ERR_I2C_ADDR_TAKEN{owner}`.
- No events.

## The client

`sys_i2c_dev_attach(owner, &dev, bus, addr, speed_Hz)` is called from the chip's `create()`: it declares the use of the bus (the registry then refuses to remove a bus while a chip on it exists and suspends the chips before their bus), then ATTACH. The address is a resource of the owner (`sys_device_res_add`): the registry lets it go when the owner is removed or its `create()` fails. Transfers (`sys_i2c_write`, `_read`, `_write_read`) and register helpers (`sys_i2c_reg_write`, `_reg_read`, `_reg_read_u16`, `_reg_write_u16`, `_reg_update`) are made from an op of the owner (its `create()` and `suspend()` too). A sequence of them (`reg_update`) is not atomic on the bus.

## Errors (`MODULE_ID_I2C` = 0x02, `MODULE_ID_I2C`)

A bus device reports its failures split by cause: `ERR_I2C_NO_ACK`, `ERR_I2C_BUS_BUSY`, else `ERR_I2C_XFER` over its cause. The client adds `ERR_I2C_BAD_ARG`, `ERR_I2C_DEV_NOT_ADDED` and `ERR_I2C_REG` (context around a register access). A missing bus is the registry's `ERR_DEV_NOT_FOUND`, a bus still in use `ERR_DEV_IN_USE`. The full list is `ERR_SYS_I2C_MAP`.

## Open

- The descriptor `contract_i2c.json` (ops for the app) is not written for this contract yet; `templates/sync.sh` has not been run for it.
