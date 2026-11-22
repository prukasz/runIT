/**
 * @file
 * @brief What lv_conf.h hooks into LVGL: the pool's first block and the assert. Defined by
 *        sys_render_lvgl.c. Not tested in TDD.
 */
#pragma once
#include <stddef.h>

/**
 * @brief LV_MEM_POOL_ALLOC: the start of the pool given by set_pool; lv_init builds its TLSF heap
 * on its first LV_MEM_SIZE bytes.
 * @param[in] size LV_MEM_SIZE
 * @return The start of the pool
 */
void *sys_render_lvgl_pool_take(size_t size);

/** @brief LV_ASSERT_HANDLER: an LVGL assert is a bug. Logs and aborts; never returns. */
void sys_render_lvgl_assert_failed(void) __attribute__((noreturn));
