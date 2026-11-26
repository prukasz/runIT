# sys_i2c_contract

Spec: `SYS_I2C_CONTRACT.MD` (this folder).

- Every change in this folder is followed by an edit of `SYS_I2C_CONTRACT.MD`, if it is needed.
- This component is the contract and the client only. A bus is a device (`device_esp32s3_i2c`, others later); nothing here includes a bus implementation, and a chip's driver includes `sys_i2c_contract.h` only.
- `CONTRACT_I2C_OPERATIONS` is the one table of the ops; the op numbers, the frames and the flags are wire format for the ops a command can reach (PROBE, SCAN, RECOVER, RAW). ATTACH, DETACH and XFER stay LOCAL.
- The owner of an address is `sys_device_caller()` at ATTACH. Do not make a transfer from a task of the driver itself: the bus refuses it (`ERR_I2C_NOT_OWNER`).
- The error tags in `ERR_SYS_I2C_MAP` are known to the app by name; changing them requires `sh templates/sync.sh`.
