# Build, configure, flash, test

This machine: ESP-IDF **v6.1**, target **esp32s3**, installed by the Espressif Installation Manager (EIM). Commands are PowerShell.

## 1. Toolchain

| Item | Path |
|---|---|
| ESP-IDF | `C:\esp\v6.1\esp-idf` (also the default in root `CMakeLists.txt`) |
| Tools | `C:\Espressif\tools` |
| Python venv | `C:\Espressif\tools\python\v6.1\venv` |
| Activation script | `C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1` |

**Activate with the EIM profile script, not `export.ps1`.** `C:\esp\v6.1\esp-idf\export.ps1` fails on this install ("Python virtual environment ... not found") because EIM keeps the venv under `C:\Espressif\tools`.

## 2. Commands

Activation lasts only for one shell call, so prefix every command with it. Always pass `-C <project>`, because the shell's working directory may not be `_SOFTWARE`.

```powershell
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1 | Out-Null; idf.py -C C:\Users\lukasz\Documents\runIT\_SOFTWARE build
```

| Task | Command (after activation) | Time |
|---|---|---|
| Build | `idf.py -C <proj> build` | full ~200 s, incremental no-op ~2 s |
| Re-run CMake / pick up Kconfig changes | `idf.py -C <proj> reconfigure` | ~26 s |
| Apply changed Kconfig **defaults** | `idf.py -C <proj> refresh-config --policy=kconfig` | ~2.5 s, non-interactive |
| Flash | `idf.py -C <proj> -p <PORT> flash` | ~10 s |
| Monitor (agent) | `& C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe .claude\skills\runit-esp\scripts\serial_capture.py <PORT> 15` | captures N seconds; see §2a |
| Monitor (user, interactive) | `idf.py -C <proj> -p <PORT> monitor` (exit `Ctrl+]`) | needs a real terminal |
| Size report | `idf.py -C <proj> size` | — |

Rules for running from the agent:
- Don't nest it in `powershell -Command "..."`. PowerShell turns native stderr lines into error records and can report a false failure. Use `2>$null` to hide CMake noise, and check `$LASTEXITCODE`.
- The output is long. Filter it with `| Select-Object -Last 20` or `| Select-String "error|warning:|Project build complete"`.
- A full build takes over 2 minutes, so use a 600000 ms timeout.
- **Flashing writes to real hardware:** only flash when the user asks. Pick the port as described in §2a.

Success looks like `Project build complete`.

**Partition table** (custom, `partitions.csv`, flash 16 MB): `nvs` 0x9000 24 KB, `phy_init` 0xf000, `factory` 0x10000 2 MB, `project` 0x210000 512 KB (NVS: the records of `sys_actions` and the stored project, [SYS_ACTIONS.MD](../../../components/sys/sys_actions/SYS_ACTIONS.MD), [SYS_PROJECT.MD](../../../components/sys/sys_project/SYS_PROJECT.MD)). Retained VM values are in `nvs`.

## 2a. Flash and monitor

**Two boards.** Bare devkit: USB-UART bridge on UART0 (GPIO 43/44). runIT PCB: "USB Serial Device", the ESP32-S3's native USB-Serial-JTAG; the console reaches the PC as the *secondary* output (`CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y`) and `sys_uart` reads `#R:` lines from it, so the command link works there too. An RTS reset re-enumerates the PCB's port: use [scripts/usb_capture.py](scripts/usb_capture.py) for boot logs (serial_capture.py loses the port). If `idf.py flash` fails with access denied right after a reset, run it again.

**Choosing the port:** COM numbers change between sessions. List ports with `[System.IO.Ports.SerialPort]::GetPortNames()`. If exactly one exists, use it and say which. If there are several, or the user named one, confirm with the user. Never guess between multiple ports.

**Closing a serial port can reset the board** (the auto-reset circuit sees DTR fall before RTS). `scripts/router_link.py` `Link` opens with DTR / RTS low and closes RTS first, so a script can end without restarting the board. Any other tool (idf monitor, a terminal) still resets it when it opens or closes the port.

**Port busy** (`PermissionError(13, 'Access is denied.')`, or flash failing right after `Serial port COMx:`): another program holds it, usually a VS Code `idf.py monitor` terminal. Find it with `Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'idf_monitor|esp_idf_monitor' }` and close it (ask the user unless they gave free access to the port).

**Flash:** `idf.py -C <proj> -p <PORT> flash 2>$null | Select-String "Chip type|Wrote|Hash of data|Hard resetting|rror|failed"`. Success is `Hash of data verified` followed by `Hard resetting via RTS pin`. Chip: ESP32-S3 (QFN56) rev v0.2.

**Monitor from the agent:** `idf.py monitor` is interactive and doesn't work without a TTY. Use [scripts/serial_capture.py](scripts/serial_capture.py), run with the IDF venv Python (it has pyserial). By default it resets the board through RTS/DTR, so the capture starts from the ROM boot log, and reads N seconds at 115200 baud. `--no-reset` attaches to a running board. Args: `<PORT> [seconds] [baud] [--no-reset]`.

**Healthy boot:** ROM → `ESP-IDF v6.1 2nd stage bootloader` → partition table → the boot steps of RUNIT.MD "Boot" → `runIT up (0 boot step(s) failed)`. On the PCB the status LED (TCA6424A pin 23) lights once the onboard devices step got that far. A failing step is logged with its name and the boot goes on.

**Open findings from the boot log:** `BLE Advertising started ...` is logged twice (a restart to apply the device name set after sync; cosmetic). The CPU runs at 160 MHz (240 MHz possible).

## 3. Kconfig changes → reconfigure

Project Kconfig files: `components/app/runit`, `core/{errors,router}`, `sys/{sys_actions,sys_ble,sys_connector,sys_device,sys_event,sys_i2c,sys_log,sys_power_manager,sys_project,sys_uart}`, `vm/vm_core`, `devices/device_gpio_esp`.

**Editing a `Kconfig` file does not trigger reconfiguration on `idf.py build`.** So:

| You changed | Run | Why |
|---|---|---|
| Added / removed / renamed an option, or changed menus/`depends on` | `idf.py reconfigure`, then `build` | Regenerates `sdkconfig`, `build/config/sdkconfig.h` and `sdkconfig.cmake` so the new `CONFIG_*` exists |
| **Changed the `default` of an existing option** | `idf.py refresh-config --policy=kconfig`, then `build` | IDF v6 keeps the value already in `sdkconfig` ("Kconfig defaults policy: use sdkconfig"). Plain `reconfigure` will **not** apply the new default |
| Want to review each default conflict | `idf.py refresh-config --policy=interactive` | Interactive; not usable from the agent |
| Changed a value through menuconfig | nothing extra | `idf.py menuconfig` is interactive and meant for the user |

`--policy=kconfig` only updates options still marked `# default:` in `sdkconfig`. Values set by the user are kept. Afterwards run `git diff sdkconfig` to review what changed.

**Watch out:** an option can be marked `# default:` with a value that differs from its current Kconfig default; the refresh then silently flips it. If that happens, restore the value and delete its `# default:` line so it counts as user-set, then `idf.py reconfigure`. An option the user set explicitly (no marker) keeps its value even when the default changes; change it by hand.

`sdkconfig` and `sdkconfig.old` are **tracked in git**. `sdkconfig.qemu.defaults` holds the QEMU overrides (§6). Treat `sdkconfig` changes as part of the commit.

Symptoms that you forgot to reconfigure: `'CONFIG_XXX' undeclared` for a newly added option, or the build still using an old default value.

## 4. Known build noise (not errors)

- Kconfig `NOTE:` lines from IDF's own Kconfig files (`BT_NIMBLE_MESH_PROVISIONER`, `FATFS_PRINT_*`, duplicate BT rename mappings), plus "user-set value ... not visible" for `PARTITION_TABLE_CUSTOM_FILENAME` and `ESP_CONSOLE_UART_BAUDRATE`.
- CMake warnings about private include dirs between `esp_wifi` and `wpa_supplicant` (inside IDF).
- The project itself has no warnings: any project warning is new and must be fixed.

## 5. clangd (per machine)

`.clangd` is tracked, but `.vscode/` is gitignored. Add this once in VS Code user/workspace settings:

```json
{ "clangd.arguments": ["--query-driver=C:/Espressif/tools/**"] }
```

Without it, clangd parses headers for the host (MSVC) target instead of `xtensa-esp32s3-elf-gcc`. That produces fake errors such as `Unknown type name '__always_inline'` or undeclared `uint8_t`. Reload clangd afterwards.

## 6. Tests

All suites run on the board (or QEMU) over the command link, with the IDF venv Python (`C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe`), and import [scripts/router_link.py](scripts/router_link.py): the host side of the router protocol (`#R:` lines, `[seq][flags][keys][payload]`, error records, streams; tag names parsed from the firmware's error maps). Run them without `-I` (they import from their own folder). Flash the current firmware first. After a flash or `esptool … hard-reset`, wait ~4 s before starting a suite.

| Script | Covers |
|---|---|
| [router_tests.py](scripts/router_tests.py) | sys (ping, urgent, dead-man, bye, stop, restart), router errors (record, route nodes, frame length), errors stream, i2c, connectors, ble, devices, actions (record, upload, read, rename, list, nested run, depth, errors, survives restart), events. `--target qemu` skips what QEMU can't do. It finds out from device 0 whether the board has its onboard devices (the PCB): then the sections that create devices from an empty table (gpio, power mux, device create / remove) and the checks that need a devkit's missing input monitor are `SKIP`ped, not failed; the PCB is covered by `board_tests.py` |
| [vm_suite.py](scripts/vm_suite.py) | the VM: wire, loader, supervisor modes and STATUS, runtime writes, telemetry, retained values, every block (VM_CORE.MD "Tests", VM_BLOCKS.MD "Tests"). `--only expr,for`, `--list`, `--skip-hw`; ~4.5 min. Parts: `vm_lib.py` (program builder, telemetry reader), `vm_t_core.py`, `vm_t_blocks.py`, `vm_t_runtime.py`, `vm_t_hw.py` |
| [vm_core_tests.py](scripts/vm_core_tests.py) | a smaller VM suite (core, the blocks, ACTION and ON_EVENT); runs on a bare devkit build |
| [board_tests.py](scripts/board_tests.py) | the runIT PCB's onboard chips; `--energise` switches the muxes and rails on with nothing connected |
| [ble_suite.py](scripts/ble_suite.py) (library: [ble_link.py](scripts/ble_link.py)) | the BLE link as the app's backend uses it, from a PC (`.venv` Python: `bleak`; the IDF env has none). `python ble_suite.py [scenario ...] [--name runit \| --address AA:BB:..] [--uart COMx]`. Scenarios: `basic` (MTU is what `sdkconfig` offers, GATT table, PING, STATUS), `frames` (13 B to the frame limit in packets of 1 to 512 B, counter wrap, over-limit frame, `ERR_BLE_FRAG`), `streams`, `latency`, `cycles` (reconnects, a disconnect mid-frame) are the default; `idle`, `stress`, `restart` (reboots the board), `gatt` (APPLY with a client connected, adds and removes a test service), `actions` (a user action stored, listed, read back, run, removed; `--action-id`, `--device`, `--pins`), `project` (a device section + autostart saved, INFO and every record checked, the board restarted and the LEDs lit by the project; reboots the board), `load --frames FILE` (engine frames to the VM router), `capture --plan FILE --out FILE` (a program, runtime writes, every stream recorded raw), `deadman` (needs `--uart`; resumes the devices the safe state suspended), `soak --minutes N` (with `--uart` it also watches for panics and reboots) on request. UUIDs, fragment bits, stream bytes, keys and ops come from `generated/`, not from the script. Opening the console port can reset the board |
| [runit-app/tests/hardware](../../../runit-app/tests/hardware) (`npm run test:hw` in `runit-app/`) | the app's TypeScript backend itself against the PCB over BLE (bleak behind `bleak_bridge.py`) with the console as a witness (`uart_tap.py`): link, services, errors and what they point at (blocks, variables, devices, pins), dead-man and safe state, program and telemetry, records and the stored project. Suites and what they change: [BACKEND.md](../../../runit-app/src/backend/BACKEND.md) "Tests". `HW_UART` (default `COM9`), `HW_NAME`, `HW_ADDRESS`. `npm run test:browser` runs the app's `WebBluetoothPlatform` in Chrome / Edge against the board (the chooser answered through the DevTools protocol), including reloading the tab |
| [router_send.py](scripts/router_send.py) | single frames by hand: `python router_send.py --port COMx "01 01" "u 01 01" "raw 07"` (keys + payload in hex; `u ` = urgent, `raw ` = bytes as-is); prints the answer and the stream frames and log lines meanwhile (`--wait` seconds) |
| [serial_capture.py](scripts/serial_capture.py), [usb_capture.py](scripts/usb_capture.py) | boot log / serial capture (§2a); `usb_capture.py` resets, reopens the re-enumerated native USB port, reads N s |

**`vm_suite.py`** drives GPIO 12 of device 0 (`VM_SUITE_PIN` changes it) and only reads the INA3221; it never starts a motor, servo or regulator block, and clears the retained values it made. Most tests step the VM one pass at a time (scan mode, ONCE) and read the result from the telemetry of that pass, so they don't depend on timing; the time blocks use wide margins and watch only objects that change rarely (an object that changes every pass floods the link).

**Bare devkit build (no chips on the board).** `CONFIG_RUNIT_ONBOARD_DEVICES=y` in the tracked `sdkconfig` makes the boot step `devices` fail on a devkit and creates GPIO ESP as device 0, which breaks the suite's CREATE tests. Build a separate variant instead of editing `sdkconfig`: write `CONFIG_RUNIT_ONBOARD_DEVICES=n` to a defaults file and run `idf.py -B build_devkit -D SDKCONFIG=build_devkit/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig;<path to that file>" build`, then flash with `idf.py -B build_devkit -p <PORT> flash`.

**QEMU** (Espressif QEMU 9.2.2, under `C:\Espressif\tools\tools\qemu-xtensa`; not on PATH after the IDF profile: call `C:\Espressif\tools\tools\qemu-xtensa\<ver>\qemu\bin\qemu-system-xtensa.exe`). QEMU has no BT controller, so it has its own build with `CONFIG_RUNIT_BLE_START=n` and no onboard devices from `sdkconfig.qemu.defaults`:

```powershell
idf.py -B build_qemu -D SDKCONFIG=build_qemu/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig;sdkconfig.qemu.defaults" build
cd build_qemu; python -m esptool --chip esp32s3 merge-bin --fill-flash-size 16MB -o qemu_flash.bin "@flash_args"
# qemu_efuse.bin: 1024 B, the esp32s3 default from idf_py_actions/qemu_ext.py (chip rev 0.3)
qemu-system-xtensa -M esp32s3 -m 32M -drive file=qemu_flash.bin,if=mtd,format=raw `
  -drive file=qemu_efuse.bin,if=none,format=raw,id=efuse -global driver=nvram.esp32s3.efuse,property=drive,value=efuse `
  -nographic -serial tcp::5555,server          # waits for the client, so the boot log is not lost
python router_tests.py --port socket://localhost:5555 --target qemu
```

Expected under QEMU: I2C bus 1 scan reports `ERR_I2C_BUS_STUCK` (no devices / pull-ups), GPIO `GET_LEVEL` readback fails (no GPIO readback).

**QEMU hangs at boot because `esp_adc` is linked** (device_gpio_esp's ADC): IDF's constructor `adc_hw_calibration` waits for SAR ADC hardware QEMU doesn't emulate; the log stops after the `eFuse: calibration efuse version` line. No Kconfig turns it off. Workaround: start QEMU with `-gdb tcp::1234 -S`, connect the suite first (the serial server waits for it), then keep [scripts/qemu_skip_adc_cal.gdb](scripts/qemu_skip_adc_cal.gdb) attached; it returns early from that constructor on every boot (so the RESTART test works too):

```powershell
xtensa-esp32s3-elf-gdb -batch -x qemu_skip_adc_cal.gdb ..\..\..\..\build_qemu\runIT.elf   # C:\Espressif\tools\xtensa-esp-elf-gdb\<ver>\xtensa-esp-elf-gdb\bin
```

**Opening the port resets a USB-Serial-JTAG board** (pyserial toggles DTR / RTS), so the first command of a run reaches a board that is booting. The boot log is dropped mid-line while nobody reads it, and the first answer then arrives glued to the fragment (`…size#R:0501…`). `router_link.py` takes the frame from the **last `#R:` in the line** and keeps the text before it as log.

Test pins on the devkit: GPIO 4–7 and 1 (ADC); nothing wired. Keep reads back-to-back when sampling a toggling pin: a ~200 ms spacing equals a 100 ms toggle's cycle and aliases.

**Unit tests:** there is no firmware unit-test target; the suites above are the regression net.
