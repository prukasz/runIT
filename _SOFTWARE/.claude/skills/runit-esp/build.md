# Build, configure, flash, test

This machine: ESP-IDF **v6.1**, target **esp32s3**, installed by the Espressif Installation Manager (EIM). Commands are PowerShell.

## 1. Toolchain

| Item | Path |
|---|---|
| ESP-IDF | `C:\esp\v6.1\esp-idf` (also the default in `runit-esp/CMakeLists.txt`) |
| Tools | `C:\Espressif\tools` |
| Python venv | `C:\Espressif\tools\python\v6.1\venv` |
| Activation script | `C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1` |

**Activate with the EIM profile script, not `export.ps1`.** `C:\esp\v6.1\esp-idf\export.ps1` fails on this install ("Python virtual environment ... not found") because EIM keeps the venv under `C:\Espressif\tools`.

## 2. Commands

Activation lasts only for one shell call, so prefix every command with it. Always pass `-C <project>`, because the shell's working directory may not be `runit-esp` (`_SOFTWARE\runit-esp`, the firmware project: its `sdkconfig`, `partitions.csv`, `build*/`, `sdkconfig.qemu.defaults` and `sdkconfig.devkit.defaults` sit there, and the relative paths below are from it).

```powershell
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1 | Out-Null; idf.py -C C:\Users\lukasz\Documents\runIT\_SOFTWARE\runit-esp build
```

| Task | Command (after activation) | Time |
|---|---|---|
| Build | `idf.py -C <proj> build` | full ~200 s, incremental no-op ~2 s |
| Re-run CMake / pick up Kconfig changes | `idf.py -C <proj> reconfigure` | ~26 s |
| A new component folder (`components/<layer>/<name>/`) | `idf.py -C <proj> reconfigure build`: a plain `build` succeeds without compiling it (check `build/compile_commands.json`) | ~1 min |
| A new or changed `idf_component.yml` (LVGL in `sys_lvgl`) | `idf.py -C <proj> reconfigure`: the component manager downloads into `managed_components/` and writes `dependencies.lock` (both not committed; needs the network once) | ~1 min |
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

**Partition table** (custom, `partitions.csv`, flash 16 MB): `nvs` 0x9000 24 KB, `phy_init` 0xf000, `ota_0` 0x10000 4 MB, `otadata` 0x410000, `ota_1` 0x420000 4 MB (the firmware slots, [SYS_OTA.MD](../../../runit-esp/components/sys/sys_ota/SYS_OTA.MD)), `project` 0x820000 512 KB (NVS: the records of `sys_actions` and the stored project, [SYS_ACTIONS.MD](../../../runit-esp/components/sys/sys_actions/SYS_ACTIONS.MD), [SYS_PROJECT.MD](../../../runit-esp/components/sys/sys_project/SYS_PROJECT.MD)), `source` 0x8a0000 1 MB (NVS: the project as the app edits it, gzipped). Retained VM values are in `nvs`.

## 2a. Flash and monitor

**Two boards.** Bare devkit: USB-UART bridge on UART0 (GPIO 43/44). runIT PCB: "USB Serial Device", the ESP32-S3's native USB-Serial-JTAG; the console reaches the PC as the *secondary* output (`CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y`) and `sys_uart` reads and writes its frames there too (SYS_UART.MD), so a client session works on either port. An RTS reset re-enumerates the PCB's port: use [scripts/usb_capture.py](scripts/usb_capture.py) for boot logs (serial_capture.py loses the port). If `idf.py flash` fails with access denied right after a reset, run it again.

**Choosing the port:** COM numbers change between sessions. List ports with `[System.IO.Ports.SerialPort]::GetPortNames()`. If exactly one exists, use it and say which. If there are several, or the user named one, confirm with the user. Never guess between multiple ports.

**Closing a serial port can reset the board** (the auto-reset circuit sees DTR fall before RTS). `scripts/router_link.py` `Link` opens with DTR / RTS low and closes RTS first, so a script can end without restarting the board. Any other tool (idf monitor, a terminal) still resets it when it opens or closes the port.

**Port busy** (`PermissionError(13, 'Access is denied.')`, or flash failing right after `Serial port COMx:`): another program holds it, usually a VS Code `idf.py monitor` terminal. Find it with `Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'idf_monitor|esp_idf_monitor' }` and close it (ask the user unless they gave free access to the port).

**Flash:** `idf.py -C <proj> -p <PORT> flash 2>$null | Select-String "Chip type|Wrote|Hash of data|Hard resetting|rror|failed"`. Success is `Hash of data verified` followed by `Hard resetting via RTS pin`. Chip: ESP32-S3 (QFN56) rev v0.2.

**Monitor from the agent:** `idf.py monitor` is interactive and doesn't work without a TTY. Use [scripts/serial_capture.py](scripts/serial_capture.py), run with the IDF venv Python (it has pyserial). By default it resets the board through RTS/DTR, so the capture starts from the ROM boot log, and reads N seconds at the console's 921600 baud (`CONFIG_ESP_CONSOLE_UART_BAUDRATE`; the ROM's own boot lines, at 115200, show as noise). `--no-reset` attaches to a running board. Args: `<PORT> [seconds] [baud] [--no-reset]`.

**Healthy boot:** ROM → `ESP-IDF v6.1 2nd stage bootloader` → partition table → the boot steps of RUNIT.MD "Boot" → `runIT up (0 boot step(s) failed)`. On the PCB the status LED (TCA6424A pin 23) lights once the board's profile has set it. A failing step is logged with its name and the boot goes on.

**Open findings from the boot log:** `BLE Advertising started ...` is logged twice (a restart to apply the device name set after sync; cosmetic). The CPU runs at 160 MHz (240 MHz possible).

## 3. Kconfig changes → reconfigure

Project Kconfig files: `runit-esp/components/app/runit`, `core/{errors,router}`, `sys/{sys_actions,sys_ble,sys_device,sys_event,sys_i2c,sys_log,sys_power_manager,sys_project,sys_remote,sys_session,sys_stream,sys_uart,sys_watch}`, `vm/vm_core`, `devices/device_esp32s3_gpio`.

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

## 5. VS Code: the ESP-IDF extension and clangd (per machine)

**The ESP-IDF extension** takes a workspace folder as its project and has no setting for a project in a subfolder. Open `runit.code-workspace` (in `_SOFTWARE`): the repo as the first folder and `runit-esp` as its own folder. Then run `ESP-IDF: Pick a Workspace Folder` once and pick `runit-esp (ESP-IDF)`; build, flash and menuconfig work on it, in `runit-esp/build` like `idf.py -C`. The extension's settings (`idf.currentSetup`, `idf.port`, `idf.flashType`) go to `runit-esp/.vscode/settings.json`, which is gitignored.

**clangd** settings are window-wide, so in the workspace only `runit.code-workspace` sets them (a folder's `.vscode/settings.json` is ignored there): `clangd.path` (the EIM's esp-clangd) and `clangd.arguments` with `--query-driver=C:/Espressif/tools/**`. The compile commands come from `runit-esp/.clangd` (`CompilationDatabase: build`). Opening `runit-esp` alone instead, put the same two settings in its `.vscode/settings.json`. Check a file from the shell: `clangd.exe --query-driver="C:/Espressif/tools/**" --check=<file>` in `runit-esp` (`All checks completed, 0 errors`).

Without the driver, clangd parses headers for the host (MSVC) target instead of `xtensa-esp32s3-elf-gcc`. That produces fake errors such as `Unknown type name '__always_inline'` or undeclared `uint8_t`. Reload clangd afterwards.

## 6. Tests

**Open: the Python suites below do not follow the firmware's session and streams yet.** `router_link.py` (and every suite on it, the app's console witness `uart_tap.py` too) sends the old HELLO (the board giving the client its id) and the CONNECTOR commands (READ, MIRROR, FORWARD, `EVENTS_ALL`), so its session does not open on the current firmware and the suites fail; `remote_tests.py` and `forward_tests.py` test connector mirrors and FORWARD, which the streams no longer have (SYS_STREAM.MD). Until they follow, the session, the streams and the watches are tested on a board by the app's suite (`npm run test:hw`: `a-link`, `h-watches`, `i-streams`).

**Host tests** (no board): unit tests of a component's real code built with `gcc` and Unity on the PC, `python test_host/run.py [component]` from `runit-esp/` (`runit-esp/test_host/HOST_TESTS.MD`: how a component gets its `host/host_test.json`, the platform stand-ins). Today: `errors`, `cmd`, `router` (its tasks run as threads). Run them for a change to a component that has them. The same stand-ins build the **screen simulator** `lvgl-playground/` (its README.md: `sys_lvgl`, `sys_device`, `sys_display`, `sys_touch` with a display driver on a simulated SPI bus and ILI9341, a simulated touch, scenarios of command frames, an SDL2 window): `.
un.ps1 -Headless -Scenario scenarios\walkthrough.txt` after a change to those components or to `test_host/`.

The suites below run on the board (or QEMU):

All suites run on the board (or QEMU) over the command link, with the IDF venv Python (`C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe`), and import [scripts/router_link.py](scripts/router_link.py): a client session on the console (SYS_SESSION.MD): the line's COBS + CRC16, the frame header, HELLO (the board gives the client its id), PINGs, a new session after a restart (`wait_reopen`), the streams subscribed (logs, errors, telemetry, events), error records; tag names parsed from the firmware's error maps. Text on the line that is no frame (the ROM, a panic, `mirror_serial`) is kept as console lines (`console_since`) and as log lines with the LOGS stream's (`logs_since`, `wait_log`). A test passes the keys without the direction byte: `frame()` adds the side the command has in `runit-app/generated/` (a command with both sides takes `dir=READ` / `dir=WRITE`); a frame stored in a record is built with `frame()` too. The Python suites test the firmware over the console; the BLE link and the app's protocol are tested by the app's own suite on the board (`runit-app/tests/hardware`, below). Run them without `-I` (they import from their own folder). Flash the current firmware first. After a flash or `esptool … hard-reset`, wait ~4 s before starting a suite. No other client of the board while a suite runs (the app over BLE, `npm run app`): its session subscribes the streams too, which changes the timing (the 1 ms PERIODIC of `vm_suite`), and a short dead-man ends its session as well.

| Script | Covers |
|---|---|
| [router_tests.py](scripts/router_tests.py) | sys (ping, urgent, dead-man, bye, stop, restart), router errors (record, route nodes, frame length), errors stream, i2c, streams, ble, devices, actions (record, upload, read, rename, list, nested run, depth, errors, survives restart), events. `--target qemu` skips what QEMU can't do. It finds out from device 0 whether the board has its onboard devices (the PCB): then the sections that create devices from an empty table (gpio, power mux, device create / remove) and the checks that need a devkit's missing input monitor are `SKIP`ped, not failed; the PCB is covered by `board_tests.py` |
| [vm_suite.py](scripts/vm_suite.py) | the VM: wire, loader, supervisor modes (stepping moves program time, block mode sends per stop, `next_block`) and STATUS, runtime writes, telemetry, retained values, every block (VM_CORE.MD "Tests", VM_BLOCKS.MD "Tests"). `--only expr,for`, `--list`, `--skip-hw`; ~4.5 min. Parts: `vm_lib.py` (program builder, telemetry reader), `vm_t_core.py`, `vm_t_blocks.py`, `vm_t_runtime.py`, `vm_t_hw.py` |
| [board_tests.py](scripts/board_tests.py) | the runIT PCB's onboard chips, a servo and a DC motor (on DRV8962 #1 channel 0: ramps, dead zone, through stop on reversing, stop, brake, settings, safe state, one way; the motor supply off, nothing turns); `--energise` switches the muxes and rails on with nothing connected |
| [runit-app/tests/hardware](../../../runit-app/tests/hardware) (`npm run test:hw` in `runit-app/`) | the app's TypeScript backend itself against the PCB over BLE (bleak behind `bleak_bridge.py`) with the console as a witness (`uart_tap.py`): link, services, errors and what they point at (blocks, variables, devices, pins), dead-man and safe state, program and telemetry, records and the stored project. Suites and what they change: [BACKEND.md](../../../runit-app/src/backend/BACKEND.md) "Tests". `HW_UART` (default `COM9`), `HW_NAME`, `HW_ADDRESS`. `npm run test:browser` runs the app's `WebBluetoothPlatform` in Chrome / Edge against the board (the chooser answered through the DevTools protocol), including reloading the tab |
| [router_send.py](scripts/router_send.py) | single requests by hand over a client session: `python router_send.py --port COMx "01 00 01" "u 01 00 01"` (`[key][dir][keys]` + payload in hex, the direction byte typed in; `u ` = urgent); prints the answer and the PUSH frames and log lines meanwhile (`--wait` seconds) |
| [serial_capture.py](scripts/serial_capture.py), [usb_capture.py](scripts/usb_capture.py) | boot log / serial capture (§2a): the text on the line as it is, each frame as one `<frame …>` line (its kind, seq, a LOGS line as text); `usb_capture.py` resets, reopens the re-enumerated native USB port, reads N s |

**`vm_suite.py`** drives GPIO 12 of device 0 (`VM_SUITE_PIN` changes it) and only reads the INA3221; it never starts a motor, servo or regulator block, and clears the retained values it made. Most tests step the VM one pass at a time (scan mode, ONCE) and read the result from the telemetry of that pass, so they don't depend on timing; the time blocks use wide margins and watch only objects that change rarely (an object that changes every pass floods the link).

**One build for every board.** `sdkconfig` is for 16 MB of flash (the PCB, an N16R8 devkit): `idf.py -p <PORT> flash`. The app for a devkit: `npm run dev:devkit` or `npm run app:devkit` in `runit-app/` (board_devkit.json).

**Two boards on a cable.** Both run the same build; the root programs its child itself. Wiring (SYS_UART.MD "The link to another board", SYS_FLASHER.MD): **root GPIO18 (link TX) to the child's U0RXD (GPIO 44, its RX pin), root GPIO17 (link RX) to its U0TXD (GPIO 43, its TX pin), root GPIO46 to its GPIO 0 (BOOT), root GPIO3 to its EN, GND to GND** (3.3 V logic, no power wire). Power the child by its native USB or 5 V, not by the port of its USB-UART bridge: a powered bridge drives U0RXD too.

1. Flash the root by cable (`idf.py -p <root port> flash`) and give it an id other than 0 (SESSION ID, then SYS RESTART): 0 is the id a new board starts with.
2. The root flashes the child: FLASHER START over any session to the root (`flashAttached` in the app's backend; `HW_FLASHER=1 npm run test:hw` runs it with its checks), STATUS until DONE. It writes the root's bootloader, partition table and firmware, erases the child's nvs and otadata, checks each region's MD5 and restarts it.
3. The child's console session hears the root's hello on the link and makes the root its master: it has no other session while that lasts (its BLE client and its own link end), id 0 until it is given another (SESSION ID to `board: 0`, then a restart). The host reaches it only through the root; later updates are OTA through the root (`writeFirmware(..., { board })`).

Run `python .claude\skills
unit-esp\scripts	ree_tests.py --port <root port>` (`--loss 20` also tests unplugging the cable), and `python .claude\skills
unit-esp\scripts
emote_tests.py --port <root port>` for the remote devices: the child's GPIO device 0 as a device of the root, async and blocking, cached reads, the child's events and errors as the twin's, dummy errors on its unconnected GPIO, the root's connectors keeping the child's streams (the latest, a queue, a full queue, the pool), and `--blink 8 [--keep]` for a program on the root (a PERIODIC of 1 s and a TOGGLE) that blinks pin 4 of the child (`--pin`); `--keep` leaves it running until the root is reset. `forward_tests.py` (the repo's `.venv` Python: it needs `bleak` and `pyserial`; `--port <root port> --ble <the root's address>`) forwards from the root to the child: the host writes a user characteristic of the root over Bluetooth, a connector with FORWARD sends it on to another characteristic (back to the host) and over the cable to a queue on the child. The root must have no other Bluetooth client (close the desktop app: it reconnects by itself and a connected board does not advertise). A host that went away (the dead-man) makes the root STOP every board, so the child's devices are suspended until a RESUME of every device (`[05][01][FF FF][03 00]` to the child): `remote_tests.py` does it first. The child's console UART is the link now, so its log text goes to the root (dropped there); its native USB still shows it. The Python suites do not follow the firmware's session yet (the note above).

**QEMU** (Espressif QEMU 9.2.2, under `C:\Espressif\tools\tools\qemu-xtensa`; not on PATH after the IDF profile: call `C:\Espressif\tools\tools\qemu-xtensa\<ver>\qemu\bin\qemu-system-xtensa.exe`). QEMU has no BT controller, so it has its own build with `CONFIG_RUNIT_BLE_START=n` from `sdkconfig.qemu.defaults`:

```powershell
idf.py -B build_qemu -D SDKCONFIG=build_qemu/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig;sdkconfig.qemu.defaults" build
cd build_qemu; python -m esptool --chip esp32s3 merge-bin --fill-flash-size 16MB -o qemu_flash.bin "@flash_args"
# qemu_efuse.bin: 1024 B, the esp32s3 default from idf_py_actions/qemu_ext.py (chip rev 0.3)
qemu-system-xtensa -M esp32s3 -m 32M -drive file=qemu_flash.bin,if=mtd,format=raw `
  -drive file=qemu_efuse.bin,if=none,format=raw,id=efuse -global driver=nvram.esp32s3.efuse,property=drive,value=efuse `
  -nographic -serial tcp::5555,server          # waits for the client, so the boot log is not lost
python router_tests.py --port socket://localhost:5555 --target qemu
```

Expected under QEMU: I2C bus 1 scan reports `ERR_I2C_BUS_STUCK` (no devices / pull-ups), a GPIO `LEVEL` read back fails (no GPIO readback).

**QEMU hangs at boot because `esp_adc` is linked** (device_esp32s3_gpio's ADC): IDF's constructor `adc_hw_calibration` waits for SAR ADC hardware QEMU doesn't emulate; the log stops after the `eFuse: calibration efuse version` line. No Kconfig turns it off. Workaround: start QEMU with `-gdb tcp::1234 -S`, connect the suite first (the serial server waits for it), then keep [scripts/qemu_skip_adc_cal.gdb](scripts/qemu_skip_adc_cal.gdb) attached; it returns early from that constructor on every boot (so the RESTART test works too):

```powershell
xtensa-esp32s3-elf-gdb -batch -x qemu_skip_adc_cal.gdb ..\..\..\..\runit-esp\build_qemu\runIT.elf   # C:\Espressif\tools\xtensa-esp-elf-gdb\<ver>\xtensa-esp-elf-gdb\bin
```

**Opening the port resets a USB-Serial-JTAG board** (pyserial toggles DTR / RTS), so the first command of a run reaches a board that is booting. `router_link.py` sends its HELLO again every 0.5 s until the board answers, and takes a board's HELLO with a new `boot_id` as a restart (a new session, the streams subscribed again). A frame starts with a 0x00 too, so text before it never spoils it. The boot's own log lines come before the session opens: a suite sees the board up by its session (`wait_boot`), not by `runIT up`.

Test pins on the devkit: GPIO 4–7 and 1 (ADC); nothing wired. Keep reads back-to-back when sampling a toggling pin: a ~200 ms spacing equals a 100 ms toggle's cycle and aliases.

**Unit tests:** there is no firmware unit-test target; the suites above are the regression net.
