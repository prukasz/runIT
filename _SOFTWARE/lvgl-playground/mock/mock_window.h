/* A window that shows the mocked panel and turns the mouse into the touch controller.
 * Only the host side: LVGL and the application never see it. */
#ifndef MOCK_WINDOW_H
#define MOCK_WINDOW_H

#include <stdbool.h>
#include <stdint.h>

int  mock_window_open(const char *title, int panel_w, int panel_h, int scale);
void mock_window_close(void);

/* Pump events: the mouse feeds mock_touch, S saves a screenshot. Returns false once the window was closed. */
bool mock_window_poll(void);
/* Copy the panel's framebuffer to the screen. */
void mock_window_present(void);

void     mock_window_set_title(const char *title);
uint32_t mock_window_ticks(void);   /* real milliseconds since start */
void     mock_window_delay(uint32_t ms);

#endif
