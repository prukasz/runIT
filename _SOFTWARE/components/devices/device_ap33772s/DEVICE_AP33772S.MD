# AP33772S — USB-C PD sink controller

Type `0x0045` (u16; answered by INFO). On the runIT board: bus 0, address 0x52 (fixed), device ID 13 (`DEVICE_ID_AP33772S`). Port of `legacy/components/devices/device_ap33772s` (2026-10-11). **Builds; not run on hardware yet** (the devkit has no AP33772S).

It negotiates a voltage / current with a USB-C PD charger and reports what the charger offers. Three contracts: POWER_USB_PD (the negotiation), POWER_VREG (the same negotiation as a wanted voltage / current, plus the output switch) and POWER_MONITOR (the sink's output, one channel). The power manager uses `GET_LIMITS` for the USB source's current (SYS_POWER_MANAGER.MD).

## Create

`[05][id u16][00 00][45 00][bus][addr][intr: u16 device, u8 pin]`

| Field | Type | Notes |
|---|---|---|
| `i2c_bus` | u8 | 0 or 1, else `ERR_DEV_RANGE` |
| `i2c_addr` | u8 | 0x52, else `ERR_DEV_RANGE` |
| `intr_pin` | `sys_io_pin_t` | INT, active low; `SYS_IO_PIN_NONE` = not wired. Made an input (`sys_io_prepare_input`: with the pull-up where the device has one; the TCA6424A has none), falling edge armed, locked (as legacy: nothing subscribes to it yet, the IO event is published for rules). Its device must answer SET_MODE, CONFIGURE_INTR and the PINS ops |

Steps: pin device declared (`sys_device_use`), keep-alive timer created, I2C probe, INT pin, then the source PDOs are read **100 ms** after power-up (the chip needs it; the one bounded wait of this driver, inside CREATE).

## Ops

| Op | Args | Answer | Notes |
|---|---|---|---|
| `0x0001` RESET, `0x0002` SUSPEND, `0x0003` RESUME | – | – | **Leave the output alone**: it is the board's own supply (the sink input), switching it off in a fault browned the ESP out (2026-10-01). SUSPEND only refuses writes |
| `0x0700` SET_SETTINGS | `{u32 voltage_mV, u32 current_mA}` | – | Ask the charger (below) |
| `0x0701` LIST_OPTIONS | – | `sys_power_usb_pd_option_t[]`: `{u8 slot, u8 type, u16 min_mV, u16 max_mV, u16 max_mA}`, empty slots left out | type 0 fixed, 1 PPS, 2 AVS; up to 13 (slots 1-7 SPR, 8-13 EPR) |
| `0x0702` GET_LIMITS | – | `{i32 mV, i32 mA}` | the highest-voltage offer; 0 / 0 with no charger |
| `0x0500` SET_ENABLE | `{u8 enable}` | – | the chip's output switch (0x12 on / 0x11 off) |
| `0x0501` SET_VOLTAGE, `0x0502` SET_CURRENT | `{u32}` | – | store the wanted value and renegotiate with both (defaults 5000 mV / 500 mA) |
| `0x0400` GET_VOLTAGE, `0x0401` GET_CURRENT, `0x0403` GET_POWER | `{u8 channel}` | i32 | 80 mV / 24 mA per LSB; allowed while suspended. Channel > 0: `ERR_POWER_BAD_CHANNEL` (legacy ignored the channel). No SET_ALERT: `ERR_DEV_UNKNOWN_OP` |
| `0xFF00` KEEPALIVE | – | – | the keep-alive timer's step; harmless from outside |

Writes while suspended: `ERR_DEV_SUSPENDED`.

## Negotiation

Asks the charger for the wanted voltage and current, in order: **PPS** if the voltage fits its range (3.3 V min when the profile has a minimum), **AVS** if it fits (15 V min), else the **highest fixed profile at or below the voltage**. A refusal of PPS / AVS (current too high, ...) falls through to the next kind. A voltage above 22 V (software limit): `ERR_DEV_RANGE`; no fixed profile at or below it: `ERR_POWER_NO_PROFILE {id, voltage_mV, lowest_mV}` (lowest_mV = the lowest fixed offer, so the caller sees the usable range). A current above the profile's: `ERR_DEV_RANGE`. Current codes: 0 below 1.25 A, n = 1.00 + 0.25n A, up to 5 A.

**AVS keep-alive**: the charger drops an AVS contract that isn't refreshed, so an AVS request is repeated every 500 ms by an `esp_timer` that calls the KEEPALIVE op (under the registry lock, like any op; the legacy had a task). A fixed or PPS request, or REMOVE, ends it. A failed repeat is reported as an error of this device.

## Changes from legacy

- Contracts as tables (POWER_USB_PD is new in `sys_power`: `sys_power_usb_pd.h`, wrappers `sys_power_usb_pd_*`); u16 IDs; registry-allocated struct.
- Errors: `ERR_DEV_RANGE` / `ERR_POWER_NO_PROFILE` instead of `ERR_INVALID_VAL_UI32` / `ERR_BASE_INVALID_STATE`; GET_POWER added; the channel is checked.
- The keep-alive is a timer and an op, not a task of its own.
- SUSPEND now sets the device's suspended flag (writes refused) where legacy did nothing at all; the output still stays as it is.
