/**
 * @file
 * @brief LVGL's configuration for sys_render (LVGL 9.3), the same on the board and on the PC. The
 *        build gives LVGL this file: LV_CONF_INCLUDE_SIMPLE with this folder on the include path
 *        (or LV_CONF_PATH), and LV_KCONFIG_IGNORE (the sdkconfig's LVGL menu is not used).
 *        Whatever is not set here is LVGL's default (lv_conf_internal.h). Not tested in TDD.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

/* ---- Colour and drawing ---- */

/* LVGL's own layers; the display's format is set per display (SYS_DISPLAY_FORMAT_*) */
#define LV_COLOR_DEPTH 16
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED 1 /* SYS_DISPLAY_FORMAT_RGB565_SWAPPED */
#define LV_DRAW_SW_SUPPORT_RGB888 1         /* SYS_DISPLAY_FORMAT_RGB888 */

/* Draw buffers: rows without padding (a flush is w x h x bpp contiguous bytes), aligned for DMA */
#define LV_DRAW_BUF_STRIDE_ALIGN 1
#define LV_DRAW_BUF_ALIGN 4

/* The default refresh period; install's refresh_hz sets the display's own */
#define LV_DEF_REFR_PERIOD 33
#define LV_DPI_DEF 130

/* ---- Memory: one pool, given by sys_render (set_pool) ----
 * lv_init builds LVGL's TLSF heap on the first LV_MEM_SIZE bytes of the pool (LV_MEM_POOL_ALLOC),
 * the driver adds the rest as a second pool. LV_MEM_SIZE + LV_MEM_POOL_EXPAND_SIZE is the largest
 * pool (it sizes TLSF's index): SYS_RENDER_HEAP_BYTES_MAX. */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (32u * 1024u)                           /* = SYS_RENDER_HEAP_BYTES_MIN */
#define LV_MEM_POOL_EXPAND_SIZE (200u * 1024u - LV_MEM_SIZE) /* = _MAX - LV_MEM_SIZE */
#define LV_MEM_POOL_INCLUDE "sys_render_lvgl_hooks.h"
#define LV_MEM_POOL_ALLOC(size) sys_render_lvgl_pool_take(size)

/* ---- System ---- */

/* No OS in LVGL: only the render task runs it, under sys_render_lock */
#define LV_USE_OS LV_OS_NONE

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 0 /* printed through the driver's print callback */

/* An assert (a failed allocation in a widget's create among them) is a bug: logged, then abort */
#define LV_ASSERT_HANDLER_INCLUDE "sys_render_lvgl_hooks.h"
#define LV_ASSERT_HANDLER sys_render_lvgl_assert_failed();

/* ---- Fonts ---- */
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* ---- Widgets: every one listed, on or off ---- */
#define LV_USE_ARC 1
#define LV_USE_BAR 1
#define LV_USE_BUTTON 1
#define LV_USE_BUTTONMATRIX 1
#define LV_USE_CHART 1
#define LV_USE_CHECKBOX 1
#define LV_USE_IMAGE 1
#define LV_USE_KEYBOARD 1
#define LV_USE_LABEL 1
#define LV_USE_LED 1
#define LV_USE_SLIDER 1
#define LV_USE_SPINBOX 1
#define LV_USE_SWITCH 1
#define LV_USE_TEXTAREA 1

#define LV_USE_ANIMIMG 0
#define LV_USE_CALENDAR 0
#define LV_USE_CANVAS 0
#define LV_USE_DROPDOWN 0
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_LINE 0
#define LV_USE_LIST 0
#define LV_USE_LOTTIE 0
#define LV_USE_MENU 0
#define LV_USE_MSGBOX 0
#define LV_USE_ROLLER 0
#define LV_USE_SCALE 0
#define LV_USE_SPAN 0
#define LV_USE_SPINNER 0
#define LV_USE_TABLE 0
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0
#define LV_USE_3DTEXTURE 0

/* Layouts: FLEX only */
#define LV_USE_FLEX 1
#define LV_USE_GRID 0

/* Themes: the default one only */
#define LV_USE_THEME_DEFAULT 1
#define LV_USE_THEME_SIMPLE 0
#define LV_USE_THEME_MONO 0

/* Examples and demos use widgets that are off */
#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS 0

#endif /* LV_CONF_H */
