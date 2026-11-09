/* The simulated ILI9341 on a 240x320 module: the controller at the other end of the SPI bus (mock_spi.c). It takes
 * the bytes as the chip does, a command (DC low) then its data (DC high), and keeps what the chip keeps: its 240x320
 * frame memory (noise at power-on), sleep, display on / off, MADCTL (MY, MX, MV, BGR), COLMOD (16 or 18 bit),
 * inversion, the column / page window and the write pointer of RAMWR. The glass (mock_panel) shows the frame memory
 * while the display is on and awake: the module's glass is BGR (a picture sent without MADCTL's BGR bit shows red and
 * blue swapped), not inverted (INVON inverts it), and mounted as mock_ili9341_set_glass_mirror says (not mirrored at
 * power-on). Ordering mistakes of a driver are counted as warnings (the datasheet's timing): a command during a reset
 * or within 5 ms of a reset, SLPOUT or SLPIN; SLPOUT or SLPIN within 120 ms of a reset while awake, SLPOUT or SLPIN;
 * an unknown command; data after a command that takes none. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MOCK_ILI9341_WIDTH  240
#define MOCK_ILI9341_HEIGHT 320

/* Power on: the glass made, the frame memory noise, every register at its reset value, asleep, display off. */
int  mock_ili9341_power_on(void);
/* The RESX pin: low holds the chip in reset, the rising edge ends it (registers to their reset values). */
void mock_ili9341_resx(bool level);
/* A command byte (DC low): ends the previous command's data. */
void mock_ili9341_command(uint8_t cmd);
/* Data bytes (DC high): the parameters of the last command, or pixels after RAMWR. */
void mock_ili9341_data(const void *data, size_t len);
/* Reads after a command: RDDPM (0x0A) answers the power mode (0x80 booster on, 0x10 awake, 0x08 normal mode, 0x04
   display on), anything else 0. With the cable off nothing drives MISO: 0xFF (its pull-up). */
void mock_ili9341_read(void *out, size_t len);
/* The module's cable, power included: off, the chip is dead (the glass dark, nothing taken, reads 0xFF); on again, it
   powers on as at start (asleep, display off, noise in its memory). */
void mock_ili9341_cable(bool on);
/* How the glass is mounted: mirrored left to right (x), top to bottom (y) against the frame memory. */
void mock_ili9341_set_glass_mirror(bool x, bool y);
/* Prints the controller's state; returns the number of warnings so far. */
unsigned mock_ili9341_print(void);
