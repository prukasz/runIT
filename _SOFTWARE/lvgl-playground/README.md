# Screen simulator (lvgl-playground)

The firmware's screen on the PC, without a board: `sys_lvgl` (the LVGL adapter, `runit-esp/components/sys/sys_lvgl/SYS_LVGL.MD`), `sys_device` with its router, `sys_display`, `sys_touch`, `sys_io`, the router, `cmd` and `errors` are compiled from `runit-esp/components` as they are, on the host stand-ins of `runit-esp/test_host` (FreeRTOS tasks as threads, HOST_TESTS.MD). The display is a real driver on a simulated SPI bus and controller: the glass shows only what the driver sent on the wire. In place of hardware:

| Part | What it is |
|---|---|
| `driver/lcd_spi.c` | The display driver, a device of contract DISPLAY (type `0x7F00`, host only), written as the board's will be: ESP-IDF's `esp_lcd` panel IO (`tx_param`, `tx_color`, `on_color_trans_done`) and `sys_io` for the reset and backlight pins. `display_cfg_t.panel` picks the controller (`sys_display_panel_e`: ILI9341 only so far). CREATE and RESET (about 125 ms): the panel IO, RESX low 1 ms (or SWRESET), 5 ms, the ILI9341's init sequence, COLMOD 16 bit, inversion, the frame memory cleared in bands of 10 rows (all while asleep), SLPOUT 120 ms after the reset, 5 ms, MADCTL (orientation, the glass's mirror, BGR from `flags`), DISPON, the backlight. FLUSH: CASET, RASET (the glass's offset in the RAM for the orientation), RAMWR queued, `done` from the transfer's callback. ORIENTATION: MADCTL. BACKLIGHT read: the duty now. HEALTH (`SYS_DISPLAY_FLAG_READABLE`): RDDPM read back over `rx_param`. SUSPEND: backlight off, SLPIN; RESUME: SLPOUT, backlight (each 120 ms after the last reset, SLPOUT or SLPIN). RESET deletes the panel IO (a hung transfer is dropped) and runs the flow again. On the board it moves to `runit-esp/components/devices/` once `sys_spi` exists |
| `mock/esp_lcd_panel_io.h`, `mock/mock_spi.c` | The `esp_lcd` panel IO API with its names and signatures, behind it the bus: `tx_param`, `rx_param` and `tx_color` wait for the colour transfer in flight (as `esp_lcd` does), a transfer takes as long as its bytes at `pclk_hz`, then the callback from the bus's thread. `fault` makes the next transfers go wrong: `refuse` (`tx_color` fails, FLUSH returns `ERR_DISPLAY_TRANSFER`), `noise` (flipped bits reach the panel and the callback comes: SPI has no acknowledge, the picture is wrong until redrawn), `hang` (the next transfer never ends, one-shot: the adapter times out and RESETs the display). `spi on` traces every command and transfer. Unlike `esp_lcd`, whose `esp_lcd_panel_io_del` waits for a hung transfer without end, the mock's drops it |
| `mock/mock_ili9341.c` | The controller on a 240x320 module: commands (DC low) and data as the chip takes them, its frame memory (noise at power-on), sleep, display on / off, MADCTL (MY, MX, MV, BGR), COLMOD (16 or 18 bit), inversion, CASET / RASET and RAMWR's write pointer, RDDPM (0x0A) read back; `cable off` kills it (the glass dark, nothing taken, MISO reads 0xFF from its pull-up), `cable on` powers it on as at start; the glass shows the frame memory while the display is on and awake. The glass is BGR (without MADCTL's BGR bit red and blue swap), mounted as `glass mirror` says. It counts a driver's mistakes against the datasheet's timing as warnings (a command during a reset or within 5 ms of a reset, SLPOUT or SLPIN; SLPOUT or SLPIN within 120 ms of a reset while awake, SLPOUT or SLPIN; an unknown command); `panel` prints its state |
| `sim/sim_gpio.c` | A device of contract IO (type `0x7F02`, no config) for the ESP32-S3 GPIO device, the module wired to it: pin 4 RESX, pin 5 the backlight LED (PWM), pins 10 CS and 11 DC (taken by the panel IO) |
| `mock/esp_heap_caps.h`, `mock/esp_memory_utils.h` | `heap_caps_calloc` and `esp_ptr_dma_capable` for the driver |
| `sim/sim_touch.c` | A device of contract TOUCH (type `0x7F01`): POINT is the finger (`mock/mock_touch.c`: the mouse, a scenario's `tap`) in the glass's pixels; capacitive (no CALIBRATION) |
| `mock/mock_panel.c` | The glass: an RGB565 framebuffer in its native orientation, the backlight, the bus's statistics, BMP shots |
| `mock/mock_window.c` | An SDL2 window showing the glass (dimmed by the backlight); the mouse is the finger |
| `sim/sim_demo.c` | A dummy screen in place of `sys_screen` (not written yet): built in C under `sys_lvgl_lock`, deleted by the adapter's RESET hook |
| `sim/sim_scenario.c` | Runs a scenario |
| `sim/sdkconfig.h` | The Kconfig defaults of the components built |
| `sketch/` | Sketches of the layers above the adapter (`sys_screen.h`, `vm_screen.h`): not built |

LVGL 9.3.0 and SDL2 come from `FetchContent`; LVGL takes `sys_lvgl`'s `lv_conf.h`, as on the board.

## Running

Needs a C compiler (gcc, WinLibs POSIX from winget on Windows), CMake ≥ 3.28, Ninja, GitHub access on the first build.

    .\run.ps1                                       # the demo in the window, scale 2
    .\run.ps1 -Scale 3                              # bigger
    .\run.ps1 -Headless -Scenario scenarios\walkthrough.txt   # the whole walk-through, no window: the console, out\*.bmp
    .\run.ps1 -Clean                                # delete build\ first
    .\run.ps1 -NoWindow                             # build without SDL2 (implies -Headless)

The script adds gcc (winget), cmake and ninja (Espressif) to `PATH` when the shell has none. By hand: `cmake -S . -B build -G Ninja`, then `cmake --build build --target window` (the demo) or `--target run` (the walk-through, headless).

In the window: the left mouse button is the finger, `R` the next ROTATION, `P` STOP / START, `X` RESET of the screen, `N` INSTALL again (display 1, touch 2) with the demo, `D` the demo screen again, `I` STATUS on the console, `S` a shot, `Esc` closes. Each key sends its frame through the router, as the app would. The title shows the flushes and kB per second over the simulated bus. The window shows the glass as it is mounted: a rotation turns the picture, not the window.

## Scenarios

`scenarios/*.txt`, one command per line, run in order on their own thread (the window stays open after the last):

| Line | What |
|---|---|
| `> 15 01 03 01` | a command frame `[key][dir][...]` in hex, through the router (`router_route`) as from a session; prints the answer (`<`) or `!` and the error chain on stderr; a STATUS of the screen is decoded |
| `wait 500` | real milliseconds |
| `tap 120 68` | the finger down at the glass's pixel for 100 ms, then up |
| `fault none` / `refuse` / `noise` / `hang` | the SPI bus's next colour transfers (`hang`: the next one only) |
| `spi on` / `spi off` | prints every command with its parameters and every colour transfer on the bus |
| `panel` | prints the ILI9341's state (sleep, display, MADCTL, COLMOD, window, warnings); fails when it counted a warning |
| `glass mirror none` / `x` / `y` / `xy` | how the module's glass is mounted against the controller's RAM |
| `cable off` / `cable on` | the display module's cable, power included: off, the panel is dead; on, it powers on asleep with noise in its memory |
| `demo` | builds the dummy screen |
| `fill 1000` | adds up to that many labels to the screen, each after `sys_lvgl_pool_check`, the pool printed every 50; stops at the first refusal |
| `shot name` | the glass into `out/name.bmp` |
| `echo text` | prints it |
| `# ...` | a comment |

DEVICE frames are `[0x05][dir][id u16][op u16][args]` (CREATE: op `0x0000`, args `[type u16][flags u8][cfg]`), the screen's `[0x15][dir][op][args]` (SYS_LVGL.MD). `pool_full.txt` fills the smallest pool with labels until `sys_lvgl_pool_check` refuses one (`ERR_LVGL_POOL_FULL`); the screen goes on drawing and turning. `demo.txt` creates the GPIO (id 3), the display (id 1, its flow on the wire traced) and the touch (id 2), INSTALLs the screen and builds the demo; `cable.txt` creates a READABLE display, takes its cable off (the adapter's HEALTH check reports `ERR_DISPLAY_LOST` once) and on again (HEALTH `RESTARTED`: the adapter resets the display and redraws). `mirror.txt` mounts the glass mirrored left to right and creates the display with `SYS_DISPLAY_FLAG_MIRROR_X`: its shots in every orientation are the right way round, as `walkthrough.txt`'s; `walkthrough.txt` goes on through touch, rotation, backlight (set and read back), HEALTH of a module that is not readable, the three ways a transfer goes wrong (refused, noise, hang), SUSPEND / RESUME, STOP / START, RESET and a second INSTALL (LVGL started again on a new pool), with STATUS between.

The simulator's exit code is 1 when a frame failed: `walkthrough.txt` has three that must (a FLUSH from a frame: `ERR_DEV_LOCAL_OP`; an INSTALL while installed: `ERR_LVGL_INSTALLED`; a pool below the smallest: `ERR_LVGL_HEAP`).
