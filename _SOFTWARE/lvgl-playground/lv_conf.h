/* Minimal LVGL configuration: everything not listed keeps LVGL's default (lv_conf_internal.h). */
#ifndef LV_CONF_H
#define LV_CONF_H

/* Pixel format of the render buffers: the same RGB565 a typical SPI/RGB panel takes. */
#define LV_COLOR_DEPTH 16

/* Memory: LVGL's own pool (no malloc of the host, as on a microcontroller). */
#define LV_USE_STDLIB_MALLOC  LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING  LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN
#define LV_MEM_SIZE           (64 * 1024)

/* One thread calls lv_timer_handler(); no OS, no locking. */
#define LV_USE_OS LV_OS_NONE

#define LV_DEF_REFR_PERIOD 33 /* ms between refreshes (~30 FPS) */
#define LV_DPI_DEF         130

/* Log through printf. */
#define LV_USE_LOG       1
#define LV_LOG_LEVEL     LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF    1

/* Software renderer only (no GPU / DMA2D / PPA). */
#define LV_USE_DRAW_SW 1

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_DEFAULT       &lv_font_montserrat_14

#endif
