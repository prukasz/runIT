# sys_i2c — I2C master on the two board buses

Component `components/sys/sys_i2c/`. Wraps the ESP-IDF `i2c_master` driver; errors use the `errors` component (module `ERR_MOD_SYS_I2C`).

| File | Contents |
|---|---|
| `sys_i2c.h` | Buses, `sys_i2c_dev_t`, transfers and register helpers |
| `errors/sys_i2c_errors.h` | `ERR_SYS_I2C_MAP`, the module's error tags (included by `sys_i2c.h`) |
| `errors/errors_sys_i2c.json` | The error messages |
| `sys_i2c.c` | The bus table (from Kconfig), the address map, the transfers |
| `router/sys_i2c_router.h/.c` | Remote diagnostics (`ROUTER_KEY_I2C` = 0x02) and the wire layouts of its ops |
| `router/contract_i2c.json` | The I2C router's descriptor (ops, args, answers) |
| `Kconfig` | Buses, timeouts, the remote bus and raw access |
| `AGENTS.md` | Rules for changing this folder |

## Buses

| Bus | Port | Default pins (Kconfig) | Role |
|---|---|---|---|
| `SYS_I2C_BUS_0` | `I2C_NUM_0` | SDA 15, SCL 16 | onboard chips (regulators, USB-PD controller) |
| `SYS_I2C_BUS_1` | `I2C_NUM_1` | SDA 40, SCL 41 | user connector |

- Pins, internal pull-ups and enable per bus, glitch filter, transfer and probe timeouts: Kconfig menu `sys_i2c` (`CONFIG_SYS_I2C_*`).
- The roles are names in Kconfig and in the comments. The rule "user devices go on the user bus" is the app's.
- `sys_i2c_init()` creates every enabled bus. It stops at the first failure (`ERR_I2C_BUS_INIT{bus, sda, scl}` over `ERR_ESP_ERR`); calling it again skips buses already created. It is the boot step `i2c` (RUNIT.MD).
- `sys_i2c_bus_ready(bus)`, `sys_i2c_bus_info(bus, &info)` (a snapshot: configuration, state, the addresses in use), `sys_i2c_probe(bus, addr)` (scan), `sys_i2c_bus_reset(bus)` (recovery of a stuck bus).
- Thread safety: the driver locks the bus for each transfer; there is no extra mutex. A sequence of several transfers (`sys_i2c_reg_update`, a chip's start-up) is not atomic on the bus. A driver's sequence runs under the device registry lock (SYS_DEVICE.MD "Concurrency"), so no other device's op interleaves with it. The remote raw ops (below) don't take that lock.
- The driver never resets a bus by itself. `sys_i2c_bus_reset` is for the caller after `ERR_I2C_BUS_STUCK` or `ERR_I2C_NO_ACK`; nothing calls it automatically yet.

## Devices

`sys_i2c_dev_t {handle, speed_Hz, addr, bus}` is embedded by value in the owner's state.

```c
sys_i2c_dev_init(&c->i2c, cfg->i2c_bus, cfg->i2c_addr, 400000);  // fill, no bus access
ERR_TRY(sys_i2c_dev_add(&c->i2c));                               // validate, probe, attach
ERR_TRY(sys_i2c_reg_read(&c->i2c, REG_ID, &id, 1));
...
ERR_TRY(sys_i2c_dev_remove(&c->i2c));                            // safe when never added
```

When init and add come together, `ERR_TRY(sys_i2c_dev_attach(&c->i2c, bus, addr, speed))` does both. `sys_i2c_dev_added(&c->i2c)` tells the owner whether the chip was built (its teardown checks it instead of a flag of its own).

- `dev_add` checks the bus and config (7-bit address, speed > 0), claims the address, probes, then attaches. Already added → no-op.
- One device per address per bus: a 128-bit map per bus (spinlock) records added addresses. A second `dev_add` on a taken address returns `ERR_I2C_ADDR_TAKEN`. The address is claimed before the probe (two concurrent adds can't both pass) and released if the probe or attach fails.
- `dev_remove` always clears `handle` and frees the address, even when the driver fails.

## Transfers

| Function | Bus traffic |
|---|---|
| `sys_i2c_write(dev, data, len)` | S addr+W data P |
| `sys_i2c_read(dev, data, len)` | S addr+R data P |
| `sys_i2c_write_read(dev, w, wlen, r, rlen)` | S addr+W w Sr addr+R r P |
| `sys_i2c_reg_write(dev, reg, data, len)` | S addr+W reg data P: two buffers in one transfer, no copy, no length limit |
| `sys_i2c_reg_read(dev, reg, data, len)` | S addr+W reg Sr addr+R data P |
| `sys_i2c_reg_read_u16` / `_write_u16(dev, reg, val, order)` | 16-bit register; `order` = `SYS_I2C_BE` (high byte first, INA3221) or `SYS_I2C_LE` (low byte first, TPS55289) |
| `sys_i2c_reg_update(dev, reg, mask, val)` | read + write of one register (not atomic against other tasks) |

Arguments are checked once in each public function; static helpers trust them.

## Errors

Tags in `ERR_SYS_I2C_MAP` (`sys_i2c.h`). Transfer and probe errors carry `bus` and `addr`, so the owner adds only its own context (device ID).

| Tag | Sev | When |
|---|---|---|
| `ERR_I2C_BAD_ARG` | HIGH | NULL pointer or zero length |
| `ERR_I2C_BAD_BUS` | HIGH | bus out of range, disabled or not created |
| `ERR_I2C_BAD_DEV_CFG` | HIGH | address > 0x7F or speed 0 |
| `ERR_I2C_BUS_INIT` | HIGH | `i2c_new_master_bus` failed (over `ERR_ESP_ERR`) |
| `ERR_I2C_BUS_STUCK` | HIGH | probe timed out: bus busy, SCL held low, no pull-ups |
| `ERR_I2C_BUS_RESET` | HIGH | bus reset failed (over `ERR_ESP_ERR`) |
| `ERR_I2C_DEV_NOT_FOUND` | HIGH | nothing acked the address on probe |
| `ERR_I2C_DEV_ADD` / `_REMOVE` | HIGH / MEDIUM | driver add/remove failed (over `ERR_ESP_ERR`) |
| `ERR_I2C_DEV_NOT_ADDED` | HIGH | transfer on a device without a handle |
| `ERR_I2C_NO_ACK` | HIGH | transfer did not finish (`ESP_ERR_INVALID_RESPONSE`: NACK or SCL timeout) |
| `ERR_I2C_BUS_BUSY` | MEDIUM | bus lock not free within the timeout (`ESP_ERR_TIMEOUT`) |
| `ERR_I2C_XFER` | HIGH | any other driver error (over `ERR_ESP_ERR`) |
| `ERR_I2C_ADDR_TAKEN` | HIGH | another device is already added at this bus + address |
| `ERR_I2C_REMOTE_DENIED` | MEDIUM | remote scan / reset / raw on a bus other than `CONFIG_SYS_I2C_REMOTE_BUS` |
| `ERR_I2C_REG` | NONE | context wrapper `{reg, write}` from the register helpers; NONE so it never raises the chain's severity |

A caller that expects a failure handles it instead of reporting it: an optional user-bus sensor that does not answer frees its `NO_ACK`.

## Remote (`sys_i2c_router`, root key `ROUTER_KEY_I2C` = 0x02)

Diagnostics only. Chips are controlled through their device drivers, never raw. The app registers it: `router_register(ROUTER_KEY_I2C, sys_i2c_router)` (runit.c). The wire layouts are the `sys_i2c_*_t` types in `sys_i2c_router.h`; `contract_i2c.json` names them.

Requests are `[0x02][dir][op][args]` (`dir` 0 read, 1 write; ROUTER.MD).

| Op | Side | Request | Response | Limits |
|---|---|---|---|---|
| `0x01` INFO | R | – | `sys_i2c_info_t` per bus (19 B each): `u8 flags` (bit0 enabled, bit1 ready, bit2 pull-up), `u8 sda`, `u8 scl`, `u32 used[4]` | all buses |
| `0x02` SCAN | R | `u8 bus` | `sys_i2c_scan_t` (16 B): `u32 found[4]`, bit n = address n acked, 0x08–0x77 | remote bus |
| `0x03` RESET | W | `u8 bus` | – | remote bus |
| `0x04` RAW | R | `sys_i2c_raw_read_args_t`: `u8 bus, addr, reg, len` (len ≥ 1) | `u8 bus, addr, reg`, then the `len` bytes read: what the write takes | remote bus, address with no device |
| | W | `sys_i2c_raw_write_args_t`: `u8 bus, addr, reg`, then data (≥ 1 B) | – | same |

- **Remote bus** = `CONFIG_SYS_I2C_REMOTE_BUS` (default 1, the user connector). Bus 0 holds the regulators and the USB-PD controller; scan, reset or raw on it → `ERR_I2C_REMOTE_DENIED{bus}`. INFO reports every bus.
- **Raw** ops are always in. They use a temporary device (`dev_add` → transfer → `dev_remove`), so an address owned by a driver fails with `ERR_I2C_ADDR_TAKEN` and a driver's cached state can't be bypassed.
- SCAN stops at the first error other than not-found (a stuck bus) and returns it.
- `sys_i2c_bus_info(bus, &info)` is the C API behind INFO.

## Open

- Raw access (RAW) can't be turned off: a Kconfig option to leave it out of release builds.
