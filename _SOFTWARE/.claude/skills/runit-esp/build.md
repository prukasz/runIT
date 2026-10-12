# Build, configure, flash

This machine: ESP-IDF **v6.1**, target **esp32s3**, installed by the Espressif Installation Manager (EIM). Commands are PowerShell.

## 1. Toolchain

| Item | Path |
|---|---|
| ESP-IDF | `C:\esp\v6.1\esp-idf` (also the default in root `CMakeLists.txt`) |
| Tools | `C:\Espressif\tools` |
| Python venv | `C:\Espressif\tools\python\v6.1\venv` |
| Activation script | `C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1` |

**Activate with the EIM profile script, not `export.ps1`.** `C:\esp\v6.1\esp-idf\export.ps1` fails on this install ("Python virtual environment ...\.espressif\python_env\idf6.1_py3.14_env not found") because EIM keeps the venv under `C:\Espressif\tools`. The old v6.0.1 path (`C:\esp\v6.0.1\...`) no longer exists.

## 2. Commands

Activation lasts only for one shell call, so prefix every command with it. Always pass `-C <project>`, because the shell's working directory may not be `_SOFTWARE`.

```powershell
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1 | Out-Null; idf.py -C C:\Users\lukasz\Documents\runIT\_SOFTWARE build
```

| Task | Command (after activation) | Measured time |
|---|---|---|
| Build | `idf.py -C <proj> build` | full ~200 s (1244 steps), incremental no-op ~2 s |
| Re-run CMake / pick up Kconfig changes | `idf.py -C <proj> reconfigure` | ~26 s (build afterwards ~2 s) |
| Apply changed Kconfig **defaults** | `idf.py -C <proj> refresh-config --policy=kconfig` | ~2.5 s, non-interactive |
| Flash | `idf.py -C <proj> -p <PORT> flash` | ~10 s write (692 KB), verified on COM3 |
| Monitor (agent) | `& C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe .claude\skills\runit-esp\scripts\serial_capture.py <PORT> 15` | captures N seconds; see §2a |
| Monitor (user, interactive) | `idf.py -C <proj> -p <PORT> monitor` (exit `Ctrl+]`) | needs a real terminal |
| Size report | `idf.py -C <proj> size` | — |

Rules for running from the agent:
- Don't nest it in `powershell -Command "..."`, and don't add `2>&1`. PowerShell 5.1 turns native stderr lines into error records and reports a false failure. Use `2>$null` to hide CMake noise, and check `$LASTEXITCODE`.
- The output is long (component list plus CMake warnings). Filter it with `| Select-Object -Last 20` or `| Select-String "error|warning:|Project build complete"`.
- A full build takes over 2 minutes, so use a 600000 ms timeout.
- **Flashing writes to real hardware:** only flash when the user asks. Pick the port as described in §2a.

Success looks like `Project build complete` and `runIT.bin binary size 0xb8c90 bytes. Smallest app partition is 0x200000 bytes. 0x147370 bytes (64%) free.` (2026-09-26).

**Partition table** (custom, `partitions.csv`, flash size 16 MB since 2026-09-26): `nvs` 0x9000 24 KB, `phy_init` 0xf000, `factory` 0x10000 2 MB, `project` 0x210000 512 KB (NVS, stored code, [SYS_PROJECT.MD](../../../components/system/sys_project/SYS_PROJECT.MD)). `nvs` kept the old offset, so settings, actions and retained values survived the switch.

## 2a. Flash and monitor

**Two boards, two ports.** Bare devkit: **COM3** (USB-UART bridge on UART0, GPIO 43/44). runIT PCB: **COM4** ("USB Serial Device" = the ESP32-S3's native USB-Serial-JTAG). On the PCB the console reaches the PC only as the *secondary* output (`CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y`); `sys_uart` also reads `#R:` lines from it, so the command link works there too. An RTS reset re-enumerates COM4: use [scripts/usb_capture.py](scripts/usb_capture.py) for boot logs (serial_capture.py loses the port), and `Link(..., reset=True)` reopens it. If `idf.py flash` fails with access denied right after a reset, run it again.

**Closing a serial port can reset the board** (the auto-reset circuit sees DTR fall before RTS). `scripts/router_link.py` `Link` opens with DTR / RTS low and closes RTS first, so a script can end without restarting the board and switching its outputs off. Any other tool (idf monitor, a terminal) still resets it when it opens or closes the port.

**Port busy** (`could not open port 'COM3': PermissionError(13, 'Access is denied.')`, or flash failing right after `Serial port COM3:`): another program holds it, usually a VS Code `idf.py monitor` terminal. Find it with `Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'idf_monitor|esp_idf_monitor' }` and close it (ask the user unless they gave free access to the port).

**Choosing the port:** when asked to flash or monitor, pick the port first. List ports with `[System.IO.Ports.SerialPort]::GetPortNames()`. If exactly one exists, use it and say which. If there are several, or the user named one, confirm with the user. Never guess between multiple ports. On this machine the board was **COM3**.

**Flash:** `idf.py -C <proj> -p <PORT> flash 2>$null | Select-String "Chip type|Wrote|Hash of data|Hard resetting|rror|failed"`. Success is `Hash of data verified` followed by `Hard resetting via RTS pin`. Detected chip: ESP32-S3 (QFN56) rev v0.2.

**Monitor from the agent:** `idf.py monitor` is interactive and doesn't work without a TTY. Use [scripts/serial_capture.py](scripts/serial_capture.py) instead, run with the IDF venv Python (it has pyserial). By default it resets the board through RTS/DTR, so the capture starts from the ROM boot log, and it reads for N seconds at 115200 baud. Add `--no-reset` to attach to a running board. Args: `<PORT> [seconds] [baud] [--no-reset]`.

Console: UART on GPIO 43/44 via the board's USB-UART bridge, 115200 baud.

**Healthy boot (reference):** ROM → `ESP-IDF v6.1 2nd stage bootloader` → partition table (nvs, phy_init, factory 1 MB) → `app_main` → `board_configuration: power limits configured` → BLE service `0xFFE0` with 4 characteristics → `BLE Advertising started ... Name set to runit` → 4 data connectors (logs 0, errors 1, telemetry 2, interface 3) bound to BLE → `sys_project: stored code: none` (or its size, frames, CRC) → interface decoders registered (10 classes: `0x01`–`0x0A`) → `runit_app: runIT boot sequence complete` (~640 ms). `Installing device: ...` lines follow for the ESP GPIO device and each device enabled by `RUNIT_BOARD_DEV_*` (`runit_board_cfg.c`) (PCB stage 1: GPIO_ESP_NATIVE 0, TCA6424A_IO_EXP 1, ADS7128_ADC 2, PCA9685_PWM_EXPANDER 3, AP33772S 13), then `onboard devices created`. On a devkit, `ERR_DEV_NOT_FOUND` from `OWNER_SYS_POWER_MONITOR_GET_VOLTAGE` (device 12) at boot is expected (no INA3221). With the UART link: `Provider registered: UART (id=2)` and connectors 1–3 bound to it.

**Open findings from the boot log:**
- ~~Flash size configured 2 MB on a 16 MB chip~~: fixed 2026-09-26 (`CONFIG_ESPTOOLPY_FLASHSIZE_16MB`, custom partition table).
- `BLE Advertising started ...` is logged twice, which suggests advertising is started twice in `sys_ble_stack.c`.
- CPU runs at 160 MHz (the ESP32-S3 supports 240 MHz).

## 3. Kconfig changes → reconfigure

Project Kconfig files: `components/{runit,utils,VM,sys_errors}/Kconfig`, `components/system/{ble,sys_event,sys_actions,sys_data_connector,sys_device,sys_i2c,sys_interface,sys_power}/Kconfig`.

**Editing a `Kconfig` file does not trigger reconfiguration on `idf.py build`.** Verified: after touching `components/core/utils/Kconfig`, the build was a 2 s no-op. So:

| You changed | Run | Why |
|---|---|---|
| Added / removed / renamed an option, or changed menus/`depends on` | `idf.py reconfigure`, then `build` | Regenerates `sdkconfig`, `build/config/sdkconfig.h` and `sdkconfig.cmake` so the new `CONFIG_*` exists |
| **Changed the `default` of an existing option** | `idf.py refresh-config --policy=kconfig`, then `build` | IDF v6 keeps the value already in `sdkconfig` ("Kconfig defaults policy: use sdkconfig"). Plain `reconfigure` will **not** apply the new default |
| Want to review each default conflict | `idf.py refresh-config --policy=interactive` | Interactive; not usable from the agent |
| Changed a value through menuconfig | nothing extra | `idf.py menuconfig` is interactive and meant for the user |

`--policy=kconfig` only updates options still marked `# default:` in `sdkconfig`. Values set by the user are kept. Afterwards run `git diff sdkconfig` to review what changed.

**Watch out:** an option can be marked `# default:` with a value that differs from its current Kconfig default (seen 2026-09-23 with a board option that has since been replaced by the in-file `RUNIT_BOARD_DEV_*` switches). The refresh then silently flips it. If that happens, restore the value and delete its `# default:` line so it counts as user-set, then `idf.py reconfigure`. An option the user set explicitly (no marker) keeps its old value even when a new default is required; change it by hand.

`sdkconfig` and `sdkconfig.old` are **tracked in git**. There is no `sdkconfig.defaults`. Treat `sdkconfig` changes as part of the commit.

Symptoms that you forgot to reconfigure: `'CONFIG_XXX' undeclared` for a newly added option, or the build still using an old default value.

## 4. Known build noise (not errors)

- Kconfig `NOTE:` lines from IDF's own Kconfig files (`BT_NIMBLE_MESH_PROVISIONER`, `FATFS_PRINT_*`, duplicate BT rename mappings), plus "user-set value ... not visible" for `PARTITION_TABLE_CUSTOM_FILENAME` and `ESP_CONSOLE_UART_BAUDRATE`.
- CMake warnings about private include dirs between `esp_wifi` and `wpa_supplicant` (inside IDF).
- **Project warnings (intentional):** 17 warnings that `sys_errors` uses another component's include directory without `REQUIRES`, one per module `errors/` folder (error maps only). These are the aggregated error/owner maps in `sys_error_codes.h` (pure data headers; decision in [architecture.md](architecture.md) §4.2). Any other project warning is new and must be fixed.

## 5. clangd (per machine)

`.clangd` is tracked, but `.vscode/` is gitignored. Add this once in VS Code user/workspace settings:

```json
{ "clangd.arguments": ["--query-driver=C:/Espressif/tools/**"] }
```

Without it, clangd parses headers for the host (MSVC) target instead of `xtensa-esp32s3-elf-gcc`. That produces fake errors such as `Unknown type name '__always_inline'` or undeclared `uint8_t`. The glob survives toolchain upgrades. Reload clangd afterwards.

## 6. Tests

### Router suite (new firmware) — QEMU first, then the board

[scripts/router_link.py](scripts/router_link.py) is the host side of the router protocol (`#R:` lines, `[seq][flags][keys][payload]`, error records, streams; tag names parsed from the firmware's X-macro maps). [scripts/router_tests.py](scripts/router_tests.py) is the suite: sys (ping, urgent, dead-man, stop, restart), router errors (record, route nodes, frame length), errors stream, i2c, conn, ble, devices (GPIO ESP, PCA9685), actions (record with / without execute, upload, read, rename, list, nested run, depth, errors, survives restart), events (notify stream via PUBLISH; rules → action and watches until 2026-10-11). Results 2026-10-10 (QEMU, with the ADC workaround below): 142/143, the one failure is GPIO readback; after the registry refactor (generic CREATE / REMOVE / INFO / OPS, self-registered types) and GPIO interrupts: 147/148, same failure; after declared dependencies + `sys_event_post`: 150/151, same failure. **Board (COM11, native USB, only GPIO ESP, 2026-10-10, after the `cmd` split and the `core/` regroup): 167/167**, also right after a reset (see "Opening the port resets the board" below); `GET_LEVEL = 1` passes on hardware, and `test_gpio_intr` runs the real edges: the output's own falling edge → GPIO ISR → event → rule → action (since 2026-10-11: → watch → events stream, not run yet). `test_gpio_intr` skips the edge checks on `--target qemu` (no GPIO readback). Run with the IDF venv Python.

**Bare devkit build (no chips on the board).** `CONFIG_RUNIT_ONBOARD_DEVICES=y` in the tracked `sdkconfig` makes the boot step `devices` fail on a devkit (`ERR_I2C_DEV_NOT_FOUND` 0x23) and creates GPIO ESP as device 0, which breaks the suite's CREATE tests. Build a separate variant instead of editing `sdkconfig`: write `CONFIG_RUNIT_ONBOARD_DEVICES=n` to a defaults file and run `idf.py -B build_devkit -D SDKCONFIG=build_devkit/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig;<path to that file>" build`, then flash with `idf.py -B build_devkit -p COM11 flash`. Router suite on it 2026-10-11: 281/281. `python vm_core_tests.py --port COM11` (VM core, the blocks, ACTION and ON_EVENT, 99/99) runs on the same build. Run both without `-I` (they import `router_link` from their own folder).

**QEMU** (Espressif QEMU 9.2.2, `qemu-xtensa` tool, already installed under `C:\Espressif\tools\tools\qemu-xtensa`). QEMU has no BT controller (its init asserts), so it has its own build with `CONFIG_RUNIT_BLE_START=n` from `sdkconfig.qemu.defaults`:

```powershell
idf.py -B build_qemu -D SDKCONFIG=build_qemu/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig;sdkconfig.qemu.defaults" build
cd build_qemu; python -m esptool --chip esp32s3 merge-bin --fill-flash-size 16MB -o qemu_flash.bin "@flash_args"
# qemu_efuse.bin: 1024 B, the esp32s3 default from idf_py_actions/qemu_ext.py (chip rev 0.3)
qemu-system-xtensa -M esp32s3 -m 32M -drive file=qemu_flash.bin,if=mtd,format=raw `
  -drive file=qemu_efuse.bin,if=none,format=raw,id=efuse -global driver=nvram.esp32s3.efuse,property=drive,value=efuse `
  -nographic -serial tcp::5555,server          # waits for the client, so the boot log is not lost
python router_tests.py --port socket://localhost:5555 --target qemu
```

`--target qemu` skips BLE runtime changes. I2C bus 1 scan reports `ERR_I2C_BUS_STUCK` under QEMU (no devices / pull-ups) — expected. GPIO `GET_LEVEL = 1` fails under QEMU (no GPIO readback) — expected.

**QEMU hangs at boot since `esp_adc` is linked** (device_gpio_esp's ADC): IDF's constructor `adc_hw_calibration` waits for SAR ADC hardware QEMU doesn't emulate; the log stops after the `eFuse: calibration efuse version` line. No Kconfig turns it off. Workaround: start QEMU with `-gdb tcp::1234 -S`, connect the suite first (the serial server waits for it), then keep [scripts/qemu_skip_adc_cal.gdb](scripts/qemu_skip_adc_cal.gdb) attached — it returns early from that constructor on every boot (so the RESTART test works too):

```powershell
xtensa-esp32s3-elf-gdb -batch -x qemu_skip_adc_cal.gdb ..\..\..\..\build_qemu\runIT.elf   # C:\Espressif\tools\xtensa-esp-elf-gdb\<ver>\xtensa-esp-elf-gdb\bin
```

`qemu-system-xtensa` is not on PATH after the IDF profile: call it as `C:\Espressif\tools\tools\qemu-xtensa\<ver>\qemu\bin\qemu-system-xtensa.exe`.

**Opening the port resets a USB-Serial-JTAG board** (the PCB, COM4 / COM11: pyserial toggles DTR / RTS), so the first command of every run reaches a board that is booting. The boot log is dropped mid-line while nobody reads it, and the first answer then arrives glued to the fragment (`…size#R:0501…`). `router_link.py` takes the frame from the **last `#R:` in the line** and keeps the text before it as log (before 2026-10-10 it required the line to *start* with `#R:`, and a suite right after a reset lost its first PING: 165/167 twice; `sys_uart`'s own parser has the same strictness, but a host sending into a quiet line never hits it). After a flash or `esptool … hard-reset`, wait ~4 s before starting a suite.

**Single packets by hand**: [scripts/router_send.py](scripts/router_send.py) — `python router_send.py --port COM10 "01 01" "u 01 01" "02 02 01" "raw 07"`. Each argument is keys + payload in hex (seq and flags are added; `u ` = urgent, `raw ` = bytes as-is). It prints the TX line, the decoded response, and the stream frames and log lines that arrived meanwhile (`--wait` seconds).

**Board**: `idf.py -p COM10 flash`, then `python router_tests.py --port COM10`. Results 2026-10-09: QEMU 36/36, board 47/47 with BLE CHAR_REMOVE (BLE advertises as "runit"; not covered yet: a BLE client — fragmentation, LOGS stream over BLE).

### VM suite (PCB)

`python vm_suite.py --port COM9` (IDF venv Python; ~4.5 min, 945 checks; `--only expr,for`, `--list`, `--skip-hw`). Needs a board that runs the current firmware (flash first). It drives GPIO 12 of device 0 (`VM_SUITE_PIN` changes it) and only reads the INA3221; it never starts a motor, servo or regulator block, and clears the retained values it made. Parts: `vm_lib.py` (program builder, telemetry reader), `vm_t_core.py`, `vm_t_blocks.py`, `vm_t_runtime.py`, `vm_t_hw.py`. Most tests step the VM one pass at a time (scan mode, ONCE) and read the result from the telemetry of that pass, so they do not depend on timing; the time blocks use wide margins and watch only objects that change rarely (an object that changes on every pass floods the link and delays the arrival times on the host). Result 2026-10-12: 945/945 (it found five firmware errors, now fixed: VM_CORE.MD "Tests").

### Legacy hardware tests over the console UART link

`CONFIG_RUNIT_UART_CONSOLE_LINK=y` makes the console UART a command link ([SYS_UART.MD](../../../components/system/sys_uart/SYS_UART.MD)): frames are `#R:<hex>` lines next to the log. Run the scripts with the IDF venv Python (`C:\Espressif	ools\python6.1env\Scripts\python.exe`); `--reset` reboots the board first.

The old test suites were deleted on 2026-10-01 (they predated the device class shape). Only the helpers remain in [scripts/](scripts/); new suites are to be written on top of `runit_link.py`.

| Script | Covers |
|---|---|
| [runit_link.py](scripts/runit_link.py) | Library + CLI: packs commands from `contracts` / `settings` JSON, matches responses by `seq`, decodes errors (names from the `errors/*.h` maps) and telemetry. `python runit_link.py COM3 --call packet_sys_io_get_level_t device_id=0 pin=4` |
| [vm_tests.py](scripts/vm_tests.py) | VM black-box suite (`python vm_tests.py COM4`, resets the board): fail-closed loader, incomplete-upload check, failed open frees the old pool, pause / resume without PERIODIC overrun, slow-motion `0x49`, quiet `suspend_all` / `resume_all`. Exit 1 on a failed check |
| [serial_capture.py](scripts/serial_capture.py) | Boot log / serial capture, see §2a |
| [usb_capture.py](scripts/usb_capture.py) | Boot log over the native USB: resets, reopens the re-enumerated port, reads N s |

Test pins on the devkit: GPIO 4–7 and 1 (ADC); nothing wired. Keep reads back-to-back when sampling a toggling pin: a ~200 ms spacing equals a 100 ms toggle's cycle and aliases.

### Unit tests

- There is no firmware unit-test target in the project.
- `SYS_ERRORS.MD` refers to `python tools/tests/test_sys_errors.py` (host test via Zig + Node), but **`tools/tests/` does not exist** in the repo. It was probably removed in the "remove all test files" cleanup (commit `47207d3`).

### BLE link (PCB)

`python scripts/ble_test.py` (system Python with `pip install bleak`; the IDF env has none): the board's advertisement (name, service 0xFFE0), GATT table, PING and a 300-byte request over the fragment framing, then `SYS RESTART` over BLE: the drop is reported and the board advertises and answers again. 13/13 on the PCB (2026-10-12). The app's browser link: `app/` (`npm run dev`, http://localhost:5180, Chrome / Edge).
