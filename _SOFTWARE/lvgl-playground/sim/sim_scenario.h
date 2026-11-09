/* A scenario: a text file of commands, one per line, run in order (README.md "Scenarios"):
 *   > 05 01 ...       a command frame [key][dir][...] in hex, run through the router as the session would; the
 *                     answer (or the error) is printed, STATUS of the screen decoded
 *   wait MS           real milliseconds
 *   tap X Y           the finger down at the glass's pixel X, Y for 100 ms, then up
 *   fault NAME        the SPI bus's next colour transfers (mock_spi.h): none, refuse, noise, hang
 *   spi on|off        prints every command and colour transfer on the SPI bus
 *   cable on|off      the display module's cable, power included (off: dead, reads 0xFF; on: powers on as at start)
 *   glass mirror M   how the module's glass is mounted: none, x, y, xy
 *   panel             prints the ILI9341's state; fails when it counted an ordering mistake of the driver
 *   demo              builds the dummy screen (sim_demo.h)
 *   fill N            adds N labels to the screen (to fill LVGL's pool)
 *   shot NAME         the glass into OUT_DIR/NAME.bmp
 *   echo TEXT         prints TEXT
 *   # ...             a comment; blank lines are skipped */
#pragma once

#include <stdint.h>

/* Runs the file; returns the number of commands that failed (a frame's error, a bad line), or -1 when it cannot be
   read. */
int sim_scenario_run(const char *path, const char *out_dir);

/* Runs one frame and prints it with its answer; returns 0 on success. */
int sim_frame(const uint8_t *req, uint32_t len);
