#pragma once

/* Drawing (lv_conf.h): what LVGL renders and how the draw buffers look to the display (SYS_DISPLAY.MD). */

/* Colour: LV_COLOR_DEPTH is LVGL's own layers; the display's format (sys_display_format_e) is set per display. */
#define LV_COLOR_DEPTH                    16
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED 1  // SYS_DISPLAY_FORMAT_RGB565_SWAPPED
#define LV_DRAW_SW_SUPPORT_RGB888         1  // SYS_DISPLAY_FORMAT_RGB888

/* Draw buffers: rows without padding (FLUSH sends w x h x bpp contiguous bytes), 4-byte aligned (DMA). */
#define LV_DRAW_BUF_STRIDE_ALIGN 1
#define LV_DRAW_BUF_ALIGN        4

/* The refresh: at most SYS_LVGL_REFRESH_HZ frames a second (a frame only when something changed); the touch is read as
 * often. The default: INSTALL's refresh_hz sets the installed screen's (SYS_LVGL_REFRESH_MIN .. _MAX, sys_lvgl.h). */
#define SYS_LVGL_REFRESH_HZ 24
#define LV_DEF_REFR_PERIOD  (1000 / SYS_LVGL_REFRESH_HZ)  // ms: 41
#define LV_DPI_DEF         130
