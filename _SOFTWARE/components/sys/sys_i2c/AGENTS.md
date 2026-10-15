# sys_i2c

Spec: `SYS_I2C.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- Do not add references to `legacy docs` unless the user asks for them.
- `ROUTER_KEY_I2C` (0x02), the op numbers (1–5), the error tags, `sys_i2c_bus_e`, `sys_i2c_order_e` and the packed layouts of the router's ops (`sys_i2c_info_t`, `sys_i2c_scan_t`, `sys_i2c_*_args_t`) are wire format: do not renumber or re-layout them.
- Changing an op, a layout, the error map, an enum or `contract_i2c.json` requires an update in `templates/`: run `sh templates/sync.sh` (regenerates `enums.json` / `errors.json`, refreshes `generated/`, validates). Commit with `--check` green. Every `struct` name in the descriptor must be a C type of `sys_i2c_router.h` or `sys_i2c.h`.
- The remote ops touch a bus only through `check_remote_bus` (INFO reports every bus and is exempt). Scan, reset and raw use `CONFIG_SYS_I2C_REMOTE_BUS` only.
- Raw access never reaches a chip a driver owns: it goes through a temporary device, so `dev_add` must keep refusing an address that is already added.
- Errors carry `bus` and `addr`; the owner adds its own context on top. Change a severity only where the caller wraps (`ERR_WRAP_SEV`).
- The bus layer takes no lock of its own across transfers. Serialisation of a chip's sequence is the device registry's (SYS_DEVICE.MD "Concurrency").
- Nothing resets a bus automatically. A new caller of `sys_i2c_bus_reset` says where it runs and why.
