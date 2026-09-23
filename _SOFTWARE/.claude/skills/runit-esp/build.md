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

**Choosing the port:** when asked to flash or monitor, pick the port first. List ports with `[System.IO.Ports.SerialPort]::GetPortNames()`. If exactly one exists, use it and say which. If there are several, or the user named one, confirm with the user. Never guess between multiple ports. On this machine the board was **COM3**.

**Flash:** `idf.py -C <proj> -p <PORT> flash 2>$null | Select-String "Chip type|Wrote|Hash of data|Hard resetting|rror|failed"`. Success is `Hash of data verified` followed by `Hard resetting via RTS pin`. Detected chip: ESP32-S3 (QFN56) rev v0.2.

**Monitor from the agent:** `idf.py monitor` is interactive and doesn't work without a TTY. Use [scripts/serial_capture.py](scripts/serial_capture.py) instead, run with the IDF venv Python (it has pyserial). By default it resets the board through RTS/DTR, so the capture starts from the ROM boot log, and it reads for N seconds at 115200 baud. Add `--no-reset` to attach to a running board. Args: `<PORT> [seconds] [baud] [--no-reset]`.

Console: UART on GPIO 43/44 via the board's USB-UART bridge, 115200 baud.

**Healthy boot (reference):** ROM → `ESP-IDF v6.1 2nd stage bootloader` → partition table (nvs, phy_init, factory 1 MB) → `app_main` → `board_configuration: power limits configured` → BLE service `0xFFE0` with 4 characteristics → `BLE Advertising started ... Name set to runit` → 4 data connectors (logs 0, errors 1, telemetry 2, interface 3) bound to BLE → interface decoders registered (9 classes now) → `runit_app: runIT boot sequence complete` (~640 ms). No device-creation logs appear while `CONFIG_RUNIT_SKIP_DEVICE_INIT=y`. (Reference log from 2026-09-22, before the power manager and events.)

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

**Watch out:** an option can be marked `# default:` with a value that differs from its current Kconfig default (seen 2026-09-23: `CONFIG_RUNIT_SKIP_DEVICE_INIT=y` under `# default:`, Kconfig default `n`). The refresh then silently flips it. If that happens, restore the value and delete its `# default:` line so it counts as user-set, then `idf.py reconfigure`. An option the user set explicitly (no marker) keeps its old value even when a new default is required; change it by hand.

`sdkconfig` and `sdkconfig.old` are **tracked in git**. There is no `sdkconfig.defaults`. Treat `sdkconfig` changes as part of the commit.

Symptoms that you forgot to reconfigure: `'CONFIG_XXX' undeclared` for a newly added option, or the build still using an old default value.

## 4. Known build noise (not errors)

- Kconfig `NOTE:` lines from IDF's own Kconfig files (`BT_NIMBLE_MESH_PROVISIONER`, `FATFS_PRINT_*`, duplicate BT rename mappings), plus "user-set value ... not visible" for `PARTITION_TABLE_CUSTOM_FILENAME` and `ESP_CONSOLE_UART_BAUDRATE`.
- CMake warnings about private include dirs between `esp_wifi` and `wpa_supplicant` (inside IDF).
- **Project warnings (intentional):** 16 warnings that `sys_errors` uses another component's include directory without `REQUIRES`, one per module `errors/` folder (error maps only). These are the aggregated error/owner maps in `sys_error_codes.h` (pure data headers; decision in [architecture.md](architecture.md) §4.2). Any other project warning is new and must be fixed.

## 5. clangd (per machine)

`.clangd` is tracked, but `.vscode/` is gitignored. Add this once in VS Code user/workspace settings:

```json
{ "clangd.arguments": ["--query-driver=C:/Espressif/tools/**"] }
```

Without it, clangd parses headers for the host (MSVC) target instead of `xtensa-esp32s3-elf-gcc`. That produces fake errors such as `Unknown type name '__always_inline'` or undeclared `uint8_t`. The glob survives toolchain upgrades. Reload clangd afterwards.

## 6. Tests

- There is no firmware unit-test target in the project.
- `SYS_ERRORS.MD` refers to `python tools/tests/test_sys_errors.py` (host test via Zig + Node), but **`tools/tests/` does not exist** in the repo. It was probably removed in the "remove all test files" cleanup (commit `47207d3`).
