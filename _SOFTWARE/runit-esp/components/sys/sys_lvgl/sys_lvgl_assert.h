#pragma once

/* LV_ASSERT_HANDLER (lv_conf.h): an LVGL assert is a bug. Logs and aborts; never returns. */
void sys_lvgl_assert_failed(void) __attribute__((noreturn));
