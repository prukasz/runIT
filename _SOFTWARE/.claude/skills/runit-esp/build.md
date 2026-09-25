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

Success looks like `Project build complete` and `runIT.bin binary size 0xab850 bytes ... (33%) free` (2026-09-23; smallest app partition is 0x100000).

## 2a. Flash and monitor

**Two boards, two ports.** Bare devkit: **COM3** (USB-UART bridge on UART0, GPIO 43/44). runIT PCB: **COM4** ("USB Serial Device" = the ESP32-S3's native USB-Serial-JTAG). On the PCB the console reaches the PC only as the *secondary* output (`CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y`); `sys_uart` also reads `#R:` lines from it, so the command link works there too. An RTS reset re-enumerates COM4: use [scripts/usb_capture.py](scripts/usb_capture.py) for boot logs (serial_capture.py loses the port), and `Link(..., reset=True)` reopens it. If `idf.py flash` fails with access denied right after a reset, run it again.

**Port busy** (`could not open port 'COM3': PermissionError(13, 'Access is denied.')`, or flash failing right after `Serial port COM3:`): another program holds it, usually a VS Code `idf.py monitor` terminal. Find it with `Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'idf_monitor|esp_idf_monitor' }` and close it (ask the user unless they gave free access to the port).

**Choosing the port:** when asked to flash or monitor, pick the port first. List ports with `[System.IO.Ports.SerialPort]::GetPortNames()`. If exactly one exists, use it and say which. If there are several, or the user named one, confirm with the user. Never guess between multiple ports. On this machine the board was **COM3**.

**Flash:** `idf.py -C <proj> -p <PORT> flash 2>$null | Select-String "Chip type|Wrote|Hash of data|Hard resetting|rror|failed"`. Success is `Hash of data verified` followed by `Hard resetting via RTS pin`. Detected chip: ESP32-S3 (QFN56) rev v0.2.

**Monitor from the agent:** `idf.py monitor` is interactive and doesn't work without a TTY. Use [scripts/serial_capture.py](scripts/serial_capture.py) instead, run with the IDF venv Python (it has pyserial). By default it resets the board through RTS/DTR, so the capture starts from the ROM boot log, and it reads for N seconds at 115200 baud. Add `--no-reset` to attach to a running board. Args: `<PORT> [seconds] [baud] [--no-reset]`.

Console: UART on GPIO 43/44 via the board's USB-UART bridge, 115200 baud.

**Healthy boot (reference):** ROM → `ESP-IDF v6.1 2nd stage bootloader` → partition table (nvs, phy_init, factory 1 MB) → `app_main` → `board_configuration: power limits configured` → BLE service `0xFFE0` with 4 characteristics → `BLE Advertising started ... Name set to runit` → 4 data connectors (logs 0, errors 1, telemetry 2, interface 3) bound to BLE → interface decoders registered (9 classes now) → `runit_app: runIT boot sequence complete` (~640 ms). `Installing device: ...` lines follow for the ESP GPIO device and each device enabled by `RUNIT_BOARD_DEV_*` (`runit_board_cfg.c`) (PCB stage 1: GPIO_ESP_NATIVE 0, TCA6424A_IO_EXP 1, ADS7128_ADC 2, PCA9685_PWM_EXPANDER 3, AP33772S 13), then `onboard devices created`. On a devkit, `ERR_DEV_NOT_FOUND` from `OWNER_SYS_POWER_MONITOR_GET_VOLTAGE` (device 12) at boot is expected (no INA3221). With the UART link: `Provider registered: UART (id=2)` and connectors 1–3 bound to it.

**Open findings from the boot log:**
- `spi_flash: Detected size(16384k) larger than the size in the binary image header(2048k)`: the board has **16 MB** flash, but `sdkconfig` has `CONFIG_ESPTOOLPY_FLASHSIZE_2MB`. 14 MB is unused (the partition table only uses about 1.1 MB).
- `BLE Advertising started ...` is logged twice, which suggests advertising is started twice in `sys_ble_stack.c`.
- CPU runs at 160 MHz (the ESP32-S3 supports 240 MHz).

## 3. Kconfig changes → reconfigure

Project Kconfig files: `components/{runit,utils,VM,sys_errors}/Kconfig`, `components/system/{ble,sys_event,sys_actions,sys_data_connector,sys_device,sys_i2c,sys_interface,sys_power}/Kconfig`.

**Editing a `Kconfig` file does not trigger reconfiguration on `idf.py build`.** Verified: after touching `components/utils/Kconfig`, the build was a 2 s no-op. So:

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

### Hardware tests over the console UART link

`CONFIG_RUNIT_UART_CONSOLE_LINK=y` makes the console UART a command link ([SYS_UART.MD](../../../components/system/sys_uart/SYS_UART.MD)): frames are `#R:<hex>` lines next to the log. The scripts in [scripts/](scripts/) drive it. Run them with the IDF venv Python (`C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe`) from the scripts folder; `--reset` reboots the board first.

| Script | Covers | Last run |
|---|---|---|
| [runit_link.py](scripts/runit_link.py) | Library + CLI (the serial reader doesn't hold the port lock while reading - it used to starve writes, commands took 0.1-17 s on an idle board): packs commands from `contracts` / `settings` JSON, matches responses by `seq`, decodes errors (names from the `errors/*.h` maps) and telemetry. `python runit_link.py COM3 --call packet_sys_io_get_level_t device_id=0 pin=4` | — |
| [devkit_tests.py](scripts/devkit_tests.py) | Interface (seq echo, short / unknown frames, 20-command burst, bad lines), GPIO (pulls, output, open-drain, ADC, error cases), device freeze / suspend / onboard protection, record + replay, IO event → user action, connector protection, power status | 2026-09-24 devkit: 21/21 |
| [vm_tests.py](scripts/vm_tests.py) | 3-block program built from the VM JSON (PERIODIC → EXPR counter + IO_TOGGLE): upload, telemetry, runtime override, pause / resume, scan / once / block / next, rewind, reset | 2026-09-24 devkit: pass |
| [persist_tests.py](scripts/persist_tests.py) | Recorded action and VM retained value across a reboot | 2026-09-24 devkit: pass |
| [pcb_tests.py](scripts/pcb_tests.py) | PCB stage 1 on COM4 or `BLE` (bleak venv, no `--reset`): status LEDs (TCA 22 / 23 blink), TCA freeze → sync, TCA inputs, ADS7128 voltages (ch7 ≈ 3.18 V), ADS7128 window alerts → event → action for both window modes, PCA9685 PWM / level, AP33772S VBUS / current / PD list | 2026-09-24 PCB: pass; 2026-09-25 BLE: pass except ADS (all 8 channels ~0 V; yesterday ch7 read 3.18 V - bench setup not checked) |
| [error_capture.py](scripts/error_capture.py) | Commands the board refuses (IO on a device without the contract, missing device, bad pin, pin without ADC, unknown class / packet, short frame); saves every raw frame as a replay fixture for the app decoder (`app/src/domain/decoder/fixtures/`, `boardCapture.test.ts`). Args: `BLE\|COM4 <out.json>` | 2026-09-25 PCB BLE: schema 0x5EF7B35C matches, 7 chains |
| [ble_pipeline_test.py](scripts/ble_pipeline_test.py) | N commands written back-to-back (write with response, as the app does) before any answer: each answered once, in order; seq 0 / 255 echo; RTT per depth. Args: `[depths] [rounds]`. Bleak venv | 2026-09-25 PCB: pass to depth 32; ~110 ms per acknowledged write, so depth > 1 gains nothing |
| [esp_pwm_test.py](scripts/esp_pwm_test.py) | ESP GPIO PWM pool rules (no scope): 9th PWM pin refused (8 channels), 5th frequency refused (4 timers), joining a running frequency, retuning a timer only one pin uses, released channel / timer reusable, frequency / duty ranges. Args: `COM4\|BLE <9 free pins> [-v]` - every pin is driven | 2026-09-25 PCB COM9 (DRV8962 #1 pins, chip off): 30/30 |
| [usb_capture.py](scripts/usb_capture.py) | Boot log over the native USB: resets, reopens the re-enumerated port, reads N s | — |
| [ble_tests.py](scripts/ble_tests.py) | BLE from the PC (bleak): MTU, origin-only responses, log / error streams, 100–524 B writes, 603 B refused, 20-write burst, reconnect. Needs a venv with `bleak` + `pyserial` (the IDF venv has no bleak) | 2026-09-24 devkit: pass |
| [ble_readv_test.py](scripts/ble_readv_test.py) | BLE re-advertising with the serial log attached: clean close and killed client, then scans until the board is visible again. Args: `COM4 [clean] [kill]`. Bleak venv | 2026-09-24 PCB: pass (killed client: ~13 s until Windows drops the link) |
| [ble_hold_test.py](scripts/ble_hold_test.py) | Holds a BLE link N s (status call every 5 s) and prints BLE / reset log lines. Args: `COM4 [seconds]`. Bleak venv | 2026-09-24 PCB: 240 s, no drop |
| [hbridge_test.py](scripts/hbridge_test.py) | DRV8962 #2: rail A 5 V, LM73100 TCA 12 on, VREF readback, ch0 +0.5; leaves it driving. `stop` brakes, opens the switch, rail A off | 2026-09-24 PCB: pass (outputs 5 V / 2.5 V avg, measured) |
| [tca_scan.py](scripts/tca_scan.py) | Reads every TCA6424A pin no function uses (as inputs). Its `KNOWN` set predates the legacy pin map (12 is now an LM73100 enable, 21 the PD INT) - update before reuse | 2026-09-24 PCB |

Test pins on the devkit: GPIO 4–7 and 1 (ADC); nothing wired. Keep reads back-to-back when sampling a toggling pin: a ~200 ms spacing equals a 100 ms toggle's cycle and aliases.

### Unit tests

- There is no firmware unit-test target in the project.
- `SYS_ERRORS.MD` refers to `python tools/tests/test_sys_errors.py` (host test via Zig + Node), but **`tools/tests/` does not exist** in the repo. It was probably removed in the "remove all test files" cleanup (commit `47207d3`).
