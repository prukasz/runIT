#ifndef LV_CONF_H
#define LV_CONF_H

/* LVGL's configuration for sys_lvgl, the same on the board and in the PC simulator: CMakeLists.txt sets LV_CONF_PATH to
 * this file and LV_KCONFIG_IGNORE (the sdkconfig's LVGL menu is not used). Whatever is not here is LVGL's default
 * (lv_conf_internal.h). SYS_LVGL.MD "LVGL configuration". */

/* Colour: LV_COLOR_DEPTH is LVGL's own layers; the display's format (sys_display_format_e) is set per display. */
#define LV_COLOR_DEPTH                    16
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED 1  // SYS_DISPLAY_FORMAT_RGB565_SWAPPED
#define LV_DRAW_SW_SUPPORT_RGB888         1  // SYS_DISPLAY_FORMAT_RGB888

/* Draw buffers: rows without padding (FLUSH sends w x h x bpp contiguous bytes), 4-byte aligned (DMA). */
#define LV_DRAW_BUF_STRIDE_ALIGN 1
#define LV_DRAW_BUF_ALIGN        4

#define LV_DEF_REFR_PERIOD 33  // ms: the refresh and the touch read
#define LV_DPI_DEF         130

/* One block for everything LVGL allocates: INSTALL takes heap_bytes, lv_init builds LVGL's TLSF heap on its first
 * LV_MEM_SIZE bytes (LV_MEM_POOL_ALLOC) and sys_lvgl adds the rest as a second pool; RESET runs lv_deinit and frees the
 * block (sys_lvgl_pool.h). Nothing of LVGL in the system heap. LV_MEM_SIZE + LV_MEM_POOL_EXPAND_SIZE is the largest
 * block: CONFIG_SYS_LVGL_HEAP_DEFAULT's range, INSTALL's limit (it sizes TLSF's index). */
#define LV_USE_STDLIB_MALLOC     LV_STDLIB_BUILTIN
#define LV_MEM_SIZE              (32u * 1024u)                    // = SYS_LVGL_HEAP_MIN
#define LV_MEM_POOL_EXPAND_SIZE  (4u * 1024u * 1024u - LV_MEM_SIZE)  // = SYS_LVGL_HEAP_MAX - LV_MEM_SIZE
#define LV_MEM_POOL_INCLUDE      "sys_lvgl_pool.h"
#define LV_MEM_POOL_ALLOC(size)  sys_lvgl_pool_take(size)

/* No OS in LVGL: sys_lvgl's task is the only one that runs it, under sys_lvgl's lock; rendering in that task. */
#define LV_USE_OS LV_OS_NONE

/* Log: printed through sys_lvgl's print callback (ESP_LOG). */
#define LV_USE_LOG   1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN

/* An assert (a failed allocation in a widget's create among them) is a bug: logged, then abort. */
#define LV_ASSERT_HANDLER_INCLUDE "sys_lvgl_assert.h"
#define LV_ASSERT_HANDLER         sys_lvgl_assert_failed();

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_DEFAULT       &lv_font_montserrat_14

#endif
