#ifndef LV_CONF_H
#define LV_CONF_H

/* LVGL's configuration for sys_lvgl, the same on the board and in the PC simulator: CMakeLists.txt sets LV_CONF_PATH to
 * this file and LV_KCONFIG_IGNORE (the sdkconfig's LVGL menu is not used). Drawing, memory, system and fonts are here,
 * the widgets in lv_conf_widgets.h; whatever neither sets is LVGL's default (lv_conf_internal.h). SYS_LVGL.MD "LVGL
 * configuration". */

/* ---- Drawing: what LVGL renders and how the draw buffers look to the display (SYS_DISPLAY.MD) ---- */

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
#define LV_DPI_DEF          130

/* ---- Memory: SYS_LVGL.MD "The pool" ---- */

/* One block for everything LVGL allocates: INSTALL takes heap_bytes, lv_init builds LVGL's TLSF heap on its first
 * LV_MEM_SIZE bytes (LV_MEM_POOL_ALLOC) and sys_lvgl adds the rest as a second pool; RESET runs lv_deinit and frees the
 * block (sys_lvgl_hooks.h). Nothing of LVGL in the system heap. LV_MEM_SIZE + LV_MEM_POOL_EXPAND_SIZE is the largest
 * block: CONFIG_SYS_LVGL_HEAP_DEFAULT's range, INSTALL's limit (it sizes TLSF's index). */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
#define LV_MEM_SIZE             (32u * 1024u)                        // = SYS_LVGL_HEAP_MIN
#define LV_MEM_POOL_EXPAND_SIZE (4u * 1024u * 1024u - LV_MEM_SIZE)  // = SYS_LVGL_HEAP_MAX - LV_MEM_SIZE
#define LV_MEM_POOL_INCLUDE     "sys_lvgl_hooks.h"
#define LV_MEM_POOL_ALLOC(size) sys_lvgl_pool_take(size)

/* ---- System: the OS, the log, the assert ---- */

/* No OS in LVGL: sys_lvgl's task is the only one that runs it, under sys_lvgl's lock; rendering in that task. */
#define LV_USE_OS LV_OS_NONE

/* Log: printed through sys_lvgl's print callback (ESP_LOG). */
#define LV_USE_LOG   1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN

/* An assert (a failed allocation in a widget's create among them) is a bug: logged, then abort (sys_lvgl_hooks.h). */
#define LV_ASSERT_HANDLER_INCLUDE "sys_lvgl_hooks.h"
#define LV_ASSERT_HANDLER         sys_lvgl_assert_failed();

/* ---- Fonts ---- */

/* A font left out here does not exist on the board; the screen layer's font enum (sys_screen_font_e: 12, 14, 16, 24)
 * names only these, and once it references them each costs flash (tens of KB; the linker drops a font nothing
 * references). */
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_DEFAULT       &lv_font_montserrat_14

/* ---- The widgets, layouts and themes ---- */

#include "lv_conf_widgets.h"

#endif
