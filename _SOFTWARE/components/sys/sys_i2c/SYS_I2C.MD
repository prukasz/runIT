# sys_i2c — I2C master on the two board buses

Component `components/sys/sys_i2c/`. Wraps the ESP-IDF `i2c_master` driver; errors use the `errors` component (module `ERR_MOD_SYS_I2C`).

## Buses

| Bus | Port | Default pins (Kconfig) | Role |
|---|---|---|---|
| `SYS_I2C_BUS_0` | `I2C_NUM_0` | SDA 15, SCL 16 | onboard chips |
| `SYS_I2C_BUS_1` | `I2C_NUM_1` | SDA 40, SCL 41 | user connector |

- Pins, internal pull-ups and enable per bus, glitch filter, transfer and probe timeouts: Kconfig menu `sys_i2c` (`CONFIG_SYS_I2C_*`).
- `sys_i2c_init()` creates every enabled bus. It stops at the first failure (`ERR_I2C_BUS_INIT{bus, sda, scl}` over `ERR_ESP_ERR`); calling it again skips buses already created. Called from `app_main` today.
- `sys_i2c_bus_ready(bus)`, `sys_i2c_probe(bus, addr)` (scan), `sys_i2c_bus_reset(bus)` (recovery of a stuck bus).
- Thread safety: the driver locks the bus for each transfer; no extra mutex.

## Devices

`sys_i2c_dev_t {handle, speed_Hz, addr, bus}` is embedded by value in the owner's state.

```c
sys_i2c_dev_init(&c->i2c, cfg->i2c_bus, cfg->i2c_addr, 400000);  // fill, no bus access
ERR_TRY(sys_i2c_dev_add(&c->i2c));                               // validate, probe, attach
ERR_TRY(sys_i2c_reg_read(&c->i2c, REG_ID, &id, 1));
...
ERR_TRY(sys_i2c_dev_remove(&c->i2c));                            // safe when never added
```

- `dev_add` checks the bus and config (7-bit address, speed > 0), claims the address, probes, then attaches. Already added → no-op.
- **One device per address per bus**: a 128-bit map per bus (spinlock) records added addresses; a second `dev_add` on a taken address returns `ERR_I2C_ADDR_TAKEN`. The address is claimed before the probe (two concurrent adds can't both pass) and released if the probe or attach fails.
- `dev_remove` always clears `handle` and frees the address, even when the driver fails.

## Transfers

| Function | Bus traffic |
|---|---|
| `sys_i2c_write(dev, data, len)` | S addr+W data P |
| `sys_i2c_read(dev, data, len)` | S addr+R data P |
| `sys_i2c_write_read(dev, w, wlen, r, rlen)` | S addr+W w Sr addr+R r P |
| `sys_i2c_reg_write(dev, reg, data, len)` | S addr+W reg data P — two buffers in one transfer, no copy, no length limit |
| `sys_i2c_reg_read(dev, reg, data, len)` | S addr+W reg Sr addr+R data P |
| `sys_i2c_reg_read_u16` / `_write_u16(dev, reg, val, order)` | 16-bit register; `order` = `SYS_I2C_BE` (high byte first: INA3221, DAC53202, ADS7128) or `SYS_I2C_LE` (AP33772S, TPS55289) |
| `sys_i2c_reg_update(dev, reg, mask, val)` | read + write of one register (not atomic against other tasks) |

Arguments are checked once in each public function; static helpers trust them.

## Errors

Tags in `ERR_SYS_I2C_MAP` (`sys_i2c.h`). Every bus/transfer error carries `bus` and `addr`, so the owner adds only its own context (device ID).

| Tag | Sev | When |
|---|---|---|
| `ERR_I2C_BAD_ARG` | HIGH | NULL pointer or zero length |
| `ERR_I2C_BAD_BUS` | HIGH | bus out of range, disabled or not created |
| `ERR_I2C_BAD_DEV_CFG` | HIGH | address > 0x7F or speed 0 |
| `ERR_I2C_BUS_INIT` | HIGH | `i2c_new_master_bus` failed (over `ERR_ESP_ERR`) |
| `ERR_I2C_BUS_STUCK` | HIGH | probe timed out — bus busy, SCL held low, no pull-ups |
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

Callers that know the real cost override severity when wrapping (`ERR_WRAP_SEV`), e.g. an optional user-bus sensor lowers a `NO_ACK` to LOW.

## Remote (`sys_i2c_router`, root key `ROUTER_KEY_I2C` = 0x02)

Diagnostics only — chips are controlled through their device drivers, never raw. Registered by the app: `router_register(ROUTER_KEY_I2C, sys_i2c_router)`.

| Op | Request | Response | Limits |
|---|---|---|---|
| `0x01` INFO | – | per bus: `u8 flags (bit0 enabled, bit1 ready, bit2 pull-up), u8 sda, u8 scl, u32 used[4]` | all buses |
| `0x02` SCAN | `u8 bus` | `u32 found[4]` (bit n = address n acked, 0x08–0x77) | remote bus |
| `0x03` RESET | `u8 bus` | – | remote bus |
| `0x04` RAW_READ | `u8 bus, addr, reg, len` | `len` bytes | remote bus, `CONFIG_SYS_I2C_REMOTE_RAW`, address with no device |
| `0x05` RAW_WRITE | `u8 bus, addr, reg`, data (≥ 1 B) | – | same |

- **Remote bus** = `CONFIG_SYS_I2C_REMOTE_BUS` (default 1, the user connector). Bus 0 holds the regulators and the USB-PD controller; scan / reset / raw on it → `ERR_I2C_REMOTE_DENIED{bus}`.
- **Raw** ops are compiled out unless `CONFIG_SYS_I2C_REMOTE_RAW` (default on; consider off in release builds) — when compiled out they answer `ERR_ROUTER_UNKNOWN_KEY`. They use a temporary device (`dev_add` → transfer → `dev_remove`), so an address owned by a driver fails with `ERR_I2C_ADDR_TAKEN` and a driver's cached state can't be bypassed.
- SCAN stops at the first error other than not-found (stuck bus) and returns it.
- `sys_i2c_bus_info(bus, &info)` is the C API behind INFO.

## Changes from legacy (`legacy/components/system/sys_i2c`)

- Bus config moved from the board code (`runit_board_cfg.c` / `runit_board_defs.h`) into Kconfig; `sys_i2c_init(void)`.
- `bool bus1` → `uint8_t bus` (`sys_i2c_bus_e`); `speed_hz` → `speed_Hz`.
- New: `sys_i2c_read`, `sys_i2c_probe`, `sys_i2c_bus_reset`, `sys_i2c_bus_ready`, `sys_i2c_reg_read_u16` / `_write_u16` (replace the per-driver byte assembly), address-conflict check in `dev_add`.
- `reg_write` uses the driver's multi-buffer transmit: no 32-byte stack copy, no `SYS_I2C_REG_MAX_LEN`.
- Errors: new `errors` API; transfer failures classified (NO_ACK / BUS_BUSY / XFER) with bus+addr instead of a bare `ERR_ESP_ERR`; probe distinguishes not-found from a stuck bus; config validated before touching the bus.
- Probe timeout from Kconfig (was a literal 50 ms); `SYS_I2C_DEFAULT_TIMEOUT_MS` → `SYS_I2C_XFER_TIMEOUT_MS`.

## Open

- No lock across transfers: `reg_update` and multi-step chip sequences are not atomic against another task using the same chip (the driver locks one transfer at a time).
- No per-bus error counters (decided: skip for now).

- No automatic recovery: nothing calls `sys_i2c_bus_reset` on `BUS_STUCK` / `NO_ACK` yet (belongs to a later error policy).
- Bus roles (onboard / user) are only names in Kconfig and comments; the app's "user devices go on the user bus" rule lives in the app and the future board layer.
