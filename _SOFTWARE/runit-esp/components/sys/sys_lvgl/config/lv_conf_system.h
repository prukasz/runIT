#pragma once

/* LVGL and the system (lv_conf.h): the OS, the log, the assert. */

/* No OS in LVGL: sys_lvgl's task is the only one that runs it, under sys_lvgl's lock; rendering in that task. */
#define LV_USE_OS LV_OS_NONE

/* Log: printed through sys_lvgl's print callback (ESP_LOG). */
#define LV_USE_LOG   1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN

/* An assert (a failed allocation in a widget's create among them) is a bug: logged, then abort (sys_lvgl_assert.h). */
#define LV_ASSERT_HANDLER_INCLUDE "sys_lvgl_assert.h"
#define LV_ASSERT_HANDLER         sys_lvgl_assert_failed();
