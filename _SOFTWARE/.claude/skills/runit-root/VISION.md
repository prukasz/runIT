# runIT — Project Vision

> Structured, typo-corrected version of the original project prompt (backed up at `../_agents_backup_2026-09-22/.agents/skills/runit-app-design/prompt.md`), plus software points from the older product description (2026-09-22). Hardware details live in [HARDWARE.md](HARDWARE.md).

## 1. What runIT is

runIT is a framework for entry-level users of electronics, embedded systems and remote control (RC) hobbyists.

- It consists of three conceptual layers: **hardware**, **embedded software** and a **phone/PC app**.
- It aims for a **plug-and-play** experience.
- It aims for **modularity**.
- It works at the intersection of low-level embedded, electronics, proprietary RC systems and IoT, taking the best part of each:

| Domain | What runIT takes from it |
|---|---|
| Low-level embedded | Performance and optional full hardware access |
| Electronics | Integration flexibility and safety-oriented hardware handling |
| RC systems | Plug-and-play remote control with a high abstraction level |
| IoT | Connection flexibility, data processing and real-world hardware integration |

**End goal:** the end user never sees a line of ordinary code when interacting with the project and the board — they only select options and place nodes on a canvas.

## 2. runIT-ESP32 (firmware)

A FreeRTOS-based application providing three main things.

### 2.1 Unified hardware interface (contracts)

- Hardware is abstracted through **contracts**. The concept of **devices** enforces one universal, device-agnostic API for hardware access (GPIO, ADC, PWM, power, …).
- Every device driver must adopt this scheme by providing an **adapter** that binds it to the contracts.
- This dramatically reduces end-user complexity: a device becomes just a **number and a unified label**, and devices can cooperate with each other through the enforced API.
- The system enforces **unified error handling**: a nested error chain with full logging and configurable reactions to errors.

### 2.2 Unified connectivity layer

- Real-time over-the-air (OTA) data and code exchange carried by an **interface-agnostic byte-packet stream**.
- Native support for **BLE** (main interface); **Wi-Fi coexistence** is planned (not yet implemented).
- Users can provide their own transports, e.g. LoRa or other sub-GHz radios.

### 2.3 High-level, purpose-built flow language (VM)

- Targets the most common and popular use cases — it is deliberately not a do-everything language.
- **Objects**: semi-static structures with easy-to-implement JSON serialization. All necessary types are supported, with full nesting and mixed-type trees.
- **Accessors**: runtime-evaluated references into objects.
- **Blocks**: code snippets operating on objects and exposing results on their own outputs, layered on top of objects/accessors.
- Design is a mix of classic **PLC** and node flows such as **Node-RED**.
- **Unique feature:** native **remote control mode** — bidirectional object data exchange that allows highly complex remote behaviour.
- The language also runs **fully offline**, like a normal microcontroller program.
- Thanks to unified device contracts, the number of blocks and their complexity is dramatically reduced.

### 2.4 Features — high-level devices, hardware-agnostic design

- Because the code enforces modularity, the underlying hardware is customizable.
- **Features** use connected devices to provide functionality for remote-controlled hardware: motors, servos, actuators.
- Features will support the most common functions of the given hardware, e.g. servo trimming, homing, motor ramp-up.

### 2.5 Result

Modular software plus real-time code upload without reflashing creates an ideal environment for **rapid prototyping in non-specialist use cases**.

runIT-ESP32 is not tied to the runIT board: it runs on a plain ESP32 devkit, even with a single custom sensor, and still provides all functionality regardless of which devices are installed.

## 3. runIT board (hardware)

The board is the concept that fully shows the power of the software.

- **Minimal wiring** — integrates most of the components that usually cause trouble in hobby projects.
- **Flexible power input, 5–20 V**: USB-C PD, standard LiPo battery, or fixed PSU.
- **Onboard protection** reduces hardware failures caused by user mistakes.
- **Onboard voltage regulators** with power-line multiplexing, protection, current monitoring, and remotely adjustable output voltage and current limit.
- **ESP32-S3 isolation** from the external environment via a separate internal supply.
- Regulators can power devices/features directly or through onboard **H-bridges (up to 100 W)** with current monitoring.
- **ADC up to 20 V**: 8 channels for measuring parameters or acting as 20 V digital inputs.
- **PWM expander**: 8 channels supplied from one of the two onboard regulators (adjustable voltage).
- **Exposed ESP32 pins** with additional protection and 3.3 V supply for normal MCU use.
- Screw terminals for durability plus plug-and-play goldpin headers.

Net effect: the only extra hardware needed is the final "feature" hardware — servos, motors, LEDs, custom sensors — all powered from one board.

## 4. runIT app

The app completes the project with the most ergonomic, user-friendly interface, ready for future changes and expansion. It acts as:

- **Remote** (controller dashboard)
- **Debugger**
- **Configurator**
- **Code IDE** with a blocks canvas

## 5. User-facing software features (from product description)

**General**
- Simple block language with built-in RC support.
- "Smart" mode — the app acts as an RC remote.
- Support for popular RC modelling parts.
- Simple configuration of connected parts, Wi-Fi and BLE.
- Easier error detection; easy access to telemetry data.
- Protections running in the background.

**Block language**
- Drag and drop.
- Automation of simple tasks.
- Send collected data over BLE or Wi-Fi.
- Dedicated control blocks for RC parts.
- Live variable preview.
- Live code editing and OTA upload.
- Easy configuration of connected parts and their power supply — set the voltage and the software handles the rest.

**RC mode**
- Link the program to a remote control on the device.
- Run complex sequences impossible with regular RC transmitters.

**Need more advanced software?**
- Implement it in C in parallel and expose it as a new block.
- Run it alongside the block program.
- Use the hardware and drivers directly and write your own code.
