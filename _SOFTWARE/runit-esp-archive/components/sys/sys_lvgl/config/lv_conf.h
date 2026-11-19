#ifndef LV_CONF_H
#define LV_CONF_H

/* LVGL's configuration for sys_lvgl, the same on the board and in the PC simulator: CMakeLists.txt sets LV_CONF_PATH to
 * this file and LV_KCONFIG_IGNORE (the sdkconfig's LVGL menu is not used). One header per subject, each says what LVGL
 * has; whatever none of them sets is LVGL's default (lv_conf_internal.h). SYS_LVGL.MD "LVGL configuration". */

#include "lv_conf_draw.h"     // colour, draw buffers, refresh
#include "lv_conf_memory.h"   // LVGL's one pool
#include "lv_conf_system.h"   // OS, log, assert
#include "lv_conf_fonts.h"    // the fonts built in
#include "lv_conf_widgets.h"  // the widgets, layouts and themes built in

#endif
