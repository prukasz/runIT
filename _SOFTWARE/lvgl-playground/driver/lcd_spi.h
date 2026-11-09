/* The display driver: a device of contract DISPLAY (SYS_DISPLAY.MD) for MIPI-DBI colour panels on SPI, written as the
 * board's driver is (ESP-IDF's esp_lcd panel IO, sys_io for the reset and backlight pins), built here on the host
 * stand-ins: the panel IO is mock_spi.c, the controller at its other end mock_ili9341.c.
 *
 * CREATE takes display_cfg_t: `panel` picks the controller's init sequence (sys_display_panel_e; ILI9341 only so far,
 * the others ERR_DEV_RANGE), `bus` the SPI host,
 * `cs` and `dc` are GPIO numbers (their pin index), `reset` and `backlight` any IO pins (SYS_IO_PIN_NONE: a software
 * reset, no backlight). The flow, from CREATE and RESET (about 125 ms): the panel IO, a hardware reset (RESX low 1 ms)
 * or SWRESET, 5 ms, the init sequence, COLMOD 16 bit, inversion, the whole frame memory cleared (all while asleep),
 * SLPOUT 120 ms after the reset, 5 ms, MADCTL for the orientation, the glass's mirror and the colour order (flags),
 * DISPON, the backlight. FLUSH: CASET, RASET (with the glass's offset in the RAM for the orientation), RAMWR with the
 * pixels queued; `done` from the panel IO's on_color_trans_done. ORIENTATION: MADCTL.
 * BACKLIGHT read: the duty now. HEALTH (with SYS_DISPLAY_FLAG_READABLE): RDDPM read back; 0xFF or 0x00 is NO_ANSWER,
 * a panel asleep or dark while it should run is RESTARTED (it lost its power).
 * SUSPEND: the backlight off, SLPIN (after the transfer in flight); RESUME: SLPOUT, the backlight. RESET deletes the
 * panel IO (a hung transfer is dropped, no `done` after it) and runs the flow again. */
#pragma once

#define LCD_SPI_DEVICE_TYPE 0x7F00u  /* host only: not in sys_device_types.h */
#define LCD_SPI_FLUSH_MAX   32768u   /* GEOMETRY flush_max: the bus's max_transfer_sz, one SPI DMA transaction */
