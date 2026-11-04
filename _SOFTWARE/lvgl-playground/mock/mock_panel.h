/* A fake LCD panel: the part of the hardware LVGL ends up talking to.
 * The interface mirrors what a real controller (ILI9341, ST7789, ...) offers over SPI / 8080 / RGB:
 * choose a window, then stream pixels into it. Behind it there is only a framebuffer in RAM. */
#ifndef MOCK_PANEL_H
#define MOCK_PANEL_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t windows;    /* set_window calls = LVGL flushes */
    uint32_t pixels;     /* pixels streamed to the panel */
    uint32_t bus_bytes;  /* bytes that would cross the bus */
} mock_panel_stats_t;

int  mock_panel_init(int width, int height);
void mock_panel_deinit(void);

/* CASET/RASET: the rectangle (inclusive corners) the next write fills row by row. */
void mock_panel_set_window(int x1, int y1, int x2, int y2);
/* RAMWR: RGB565, native byte order, `len` bytes. */
void mock_panel_write(const void *pixels, size_t len);

const mock_panel_stats_t *mock_panel_stats(void);
/* The pixels as the panel shows them (width x height, RGB565), for the window and the screenshots. */
const uint16_t *mock_panel_framebuffer(void);
/* Save the whole framebuffer as a 24-bit BMP; returns 0 on success. */
int mock_panel_save_bmp(const char *path);

#endif
