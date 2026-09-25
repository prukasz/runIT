# runIT Board — Hardware Capabilities

Current hardware revision. Merged from two older product descriptions (texts are dated, hardware is current) and the firmware device catalog. Sources of truth, in order:

1. [components/runit/runit_board_defs.h](../../../components/runit/runit_board_defs.h) — device IDs, I2C buses, power limits, BLE UUIDs
2. [components/runit/runit_board_cfg.c](../../../components/runit/runit_board_cfg.c) — how each onboard chip is created and wired
3. `data-structures/devices/*.generated.json` — per-device install packet + contract ops (client-facing catalog)

If this file and those sources disagree, the sources win — update this file.

## 1. Why the board exists

> **YOU can run IT.**

runIT is a controller at the intersection of **IoT**, **RC modelling** and **automation**. Off-the-shelf RC systems are convenient but inflexible for advanced or autonomous projects. Arduino/ESP32 are flexible, but driving high-power loads ends in a fragile tangle of wires and add-on modules. runIT is the missing link: ESP32 programmability with plug-and-play, safe, tidy high-power I/O.

Users connect standard RC parts (servos, ESCs, actuators), popular sensors and LED strips, build a control UI in the app, and make the project "smart" and networked with the block language.

## 2. Capabilities

### 2.1 Power input — USB-C PD + universal input
- USB-C PD 3.0, up to **20 V / 5 A** (AP33772S).
- Battery or DC PSU input **5–20 V**, with voltage and current measurement.
- Both inputs can be connected at the same time.
- Firmware board limit (hardware trip): 20 V / 5.5 A (`RUNIT_BOARD_POWER_LIMIT_*`).
- Always-on load: the 3.3 V buck for the ESP32, up to 1 A (reserved in the budget).
- The active source (USB-C / PSU / battery) is marked by pins on the eFuse side; pin numbers still to confirm (§7).

### 2.2 Adjustable DC-DC converters (2× TPS55289)
- Output freely adjustable **4.5–20 V**, set remotely.
- Short-circuit and overload protection, programmable current limit.
- Direct outputs on the board — each rail doubles as an adjustable bench-style PSU.
- One of the two rails also supplies the PWM headers (see 2.4).

### 2.3 PowerIO
- **4 configurable H-bridges (8 outputs)** — 2× DRV8962, up to 20 V / 5 A per output, **100 W** total.
- Full-bridge (motors) or half-bridge (solenoids, loads) topology.
- **Power-source selection per H-bridge group** (4× LM73100): feed from either DC-DC rail or directly from the input — drive loads with different voltages straight from the board.
- Current measurement (IPROPI) and **constant-current mode** (VREF current limit, set by DAC53202 — one separate DAC channel per DRV8962).
- **8 analog/digital inputs** (ADS7128) working directly with up to **20 V**, no external dividers needed.

### 2.4 PWM expander (PCA9685)
- **8 dedicated channels** (CH 0–7) with 3-pin servo headers — servos and ESCs without using any other channels.
- Header supply comes from **one of the two TPS55289 rails**, so the servo voltage is adjustable, not a fixed 5 V.
- CH 8–15 are internal: they drive the second DRV8962.

### 2.5 Protection
- eFuses (2× TPS259474).
- Reverse-polarity protection.
- ESD protection.
- Isolated 3.3 V supply for the ESP32-S3.
- Adjustable input current limit.
- Voltage and current monitoring with alerts (INA3221: both TPS55289 rails + whole-board input).

### 2.6 ESP32-S3
- Wi-Fi + BLE — either one or both at once.
- Exposed GPIO including SPI and I2C (user I2C bus), with protection and 3.3 V supply.
- Not limited to onboard peripherals — connect popular sensors.
- ESP-IDF ecosystem — easy to modify for your own needs.

### 2.7 Connectors
- Screw terminals (power, loads) + goldpin headers (servos, sensors).

## 3. Onboard device catalog (firmware view)

All onboard chips are **static devices** created at boot by `runit_board_devices_init()`. Each is reached by its device ID through a system contract — the app and VM never talk to the chip directly.

| ID | Chip | Contract | I2C (bus 0) | Board wiring | Role |
|---|---|---|---|---|---|
| 0 | ESP32-S3 native GPIO | Digital/Analog IO | — | — | Digital I/O, edge interrupts, ADC reads, PWM (LEDC: 8 pins, 4 frequencies at once by default - Kconfig masks keep timers / channels for other LEDC users; no DAC) |
| 1 | TCA6424A | Digital/Analog IO | 0x23 | INT → ESP 9, RST → ESP 8 | 24-bit expander, internal control: LM73100 switches, eFuses, regulator enables, interrupts, PCA OE |
| 2 | ADS7128 | Digital/Analog IO | 0x10 | ALERT → ESP 42, Vref 20 V | 8 inputs up to 20 V, window comparator |
| 3 | PCA9685 | Digital/Analog IO | 0x60 | OE → TCA 0 | CH 0–7 user PWM headers, CH 8–15 → DRV8962 #2 (12-bit duty, one shared frequency) |
| 4 | DAC53202 | Digital/Analog IO | 0x48 (A0 = GND) | — | OUT0/OUT1 → VREF of DRV8962 #1/#2; VDD reference, gain 1x (full scale 3.3 V) |
| 5 | DRV8962 #1 | H-bridge | — | IN1-4 → ESP 21/47/48/45, EN1-4 → ESP 38/39/2/1, IPROPI1-4 → ESP 7/6/5/4, nSLEEP → TCA 9, nFAULT → TCA 11, VM ← TCA 14 (VSUP) / 15 (rail B) | 2 full bridges; **off**: ESP GPIO has no PWM yet |
| 6 | DRV8962 #2 | H-bridge | — | IN1-4 → PCA CH 8-11, EN1-4 → PCA CH 12-15, nSLEEP → TCA 8, nFAULT → TCA 10, VM ← TCA 12 (rail A) / 13 (VSUP), IPROPI resistors only | 2 full bridges, PWM at the PCA frequency (1 kHz) |
| 10 | TPS55289 #0 | Voltage Regulator | 0x74 | INT → TCA 1, EN → TCA 17 | Adjustable rail A |
| 11 | TPS55289 #1 | Voltage Regulator | 0x75 | INT → TCA 2, EN → TCA 16 | Adjustable rail B |
| 12 | INA3221 | Power Monitor | 0x40 | CRIT → TCA 5, WARN → TCA 6 | V/I of rail A, rail B and whole-board input, with alerts |
| 13 | AP33772S | USB-C Power Delivery | 0x52 | INT → TCA 21 | PD sink: request/read PDOs, negotiated V/I, protection events |
| — | LM73100 ×4 | — (not modelled) | — | EN (high = on) → TCA 12 DRV2←rail A, 13 DRV2←VSUP, 14 DRV1←VSUP, 15 DRV1←rail B; all low at boot | H-bridge VM source (one VM node per DRV8962: one switch at a time) |
| — | TPS259474 ×2 | — (not modelled) | — | PG → TCA 3 (VUSB_OK), TCA 4 (VEXT_OK) | Input eFuses |

Also driven at boot: TCA pins 22 and 23 as push-pull outputs (roles not documented in code).

**Design constraints worth remembering:**
- The PCA9685 has one PWM frequency for all 16 channels, so the servo headers (CH 0–7) and DRV8962 #2 (CH 8–15) always share a frequency. Servos typically want ~50 Hz; motor drive usually runs much faster.
- The servo header voltage follows the TPS55289 rail that feeds it — changing that rail's voltage changes the servo supply.

### Contract ops per device (from generated catalog)

- **GPIO ESP / TCA6424A** — set mode, release, write level, toggle, read, edge interrupt; ESP also ADC read.
- **ADS7128** — read channel (mV), configure window comparator alert, reset channel. ADC-only in firmware.
- **PCA9685** — set duty (clamped to 4095), set frequency (shared), level → off/full duty, turn off.
- **DAC53202** — set voltage, read back last voltage, power off channel.
- **TPS55289** — set voltage, set current limit, enable/disable. Protection faults are published as power events.
- **INA3221** — read bus voltage, read current, set warning/critical current alert (published as a power event).
- **AP33772S** — request PD profile, list offered profiles, read negotiated V/I, output control (single rail, channel 0).

## 4. Buses and connectivity

| Bus / link | Details |
|---|---|
| I2C 0 (internal) | SDA 15, SCL 16 — all onboard chips |
| I2C 1 (user) | SDA 40, SCL 41 — external user sensors/devices |
| BLE | Service `0xFFE0`: RX `0xFFE2` (write, interface), TX `0xFFE1` (notify: interface + telemetry), LOGS `0xFFE3` (notify: logs + errors), STATUS `0xFFE4` (notify) |
| Wi-Fi | Hardware-capable (ESP32-S3); firmware not implemented |

## 5. Firmware coverage gaps

| Hardware | Firmware status |
|---|---|
| DRV8962 ×2 (H-bridges) | #2 created (ID 6); #1 (ID 5) written but off until its ESP pins are confirmed; no generated JSON descriptor |
| DAC53202 | Created (ID 4); used as DRV8962 VREF |
| LM73100 ×4 (power-source selection) | No firmware device or API — needs a model for "which source feeds which H-bridge" |
| TPS259474 eFuses ×2 | No firmware device |
| ADS7128 digital mode | Hardware supports analog **and** digital inputs; adapter is ADC-only |
| PCA9685 CH 8–15 | Reserved for DRV8962 #2 — must not be exposed to users as free PWM |
| Wi-Fi | Not implemented |

## 6. Confirmed by hardware owner (2026-09-22)

- TCA6424A controls the LM73100 switches and the TPS259474 eFuses.
- INA3221 monitors both TPS55289 rails and the whole-board power.
- PCA9685: CH 0–7 exposed, CH 8–15 drive the second DRV8962.
- DAC53202 sets DRV8962 VREF — separate channel per DRV8962.
- PWM header supply comes from one of the two TPS55289 rails.
- H-bridge rating: 100 W.

## 7. Deferred questions (not needed for now)

- ~~TCA6424A pins for LM73100 / eFuses~~ — from the legacy firmware header (2026-09-24), see §3. TCA 7 = DRV8962 OCPM (both chips).
- ~~INA3221 channel order~~ — measured 2026-09-24: ch0 = rail B (TPS55289 0x75), ch1 = board input (shunt reversed by design), ch2 = rail A (0x74).
- DRV8962 IPROPI resistor value (3.09 kΩ assumed).
- Which of the two TPS55289 rails feeds the PWM headers.
