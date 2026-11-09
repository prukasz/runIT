/* The glass: what the module shows, an RGB565 framebuffer in its native orientation, and the backlight. The
 * controller (mock_ili9341.c) puts its frame memory on it; the window (mock_window.c) and the shots read it. */
#ifndef MOCK_PANEL_H
#define MOCK_PANEL_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t transfers;  /* colour transfers on the bus (a FLUSH, a chunk of a clear) */
    uint32_t bus_bytes;  /* their bytes */
    uint32_t changes;    /* the picture or the backlight changed: the window presents again */
} mock_panel_stats_t;

int  mock_panel_init(int width, int height);
void mock_panel_deinit(void);

/* One pixel of the glass (native orientation), RGB565 native byte order. */
void mock_panel_put(int x, int y, uint16_t rgb565);
/* A colour transfer of `bytes` went over the bus (mock_spi.c): the statistics; the picture changed. */
void mock_panel_account(uint32_t bytes);
/* The picture changed without a transfer (the display turned on or off, MADCTL). */
void mock_panel_redraw(void);
/* Backlight 0 .. 4095 (the LED's PWM duty): the window dims the picture. */
void mock_panel_set_brightness(uint16_t duty);
uint16_t mock_panel_brightness(void);

/* The glass's size; 0 x 0 before mock_panel_init. */
void mock_panel_size(int *width, int *height);

const mock_panel_stats_t *mock_panel_stats(void);
/* The pixels as the panel shows them (width x height, RGB565), for the window and the screenshots. */
const uint16_t *mock_panel_framebuffer(void);
/* Save the whole framebuffer as a 24-bit BMP; returns 0 on success. */
int mock_panel_save_bmp(const char *path);

#endif
