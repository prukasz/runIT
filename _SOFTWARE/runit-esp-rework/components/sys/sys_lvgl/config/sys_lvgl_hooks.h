#pragma once

#include <stddef.h>

/* What LVGL's own sources call in sys_lvgl (lv_conf.h): its pool and its assert. */

/**
 * @brief LVGL's block (lv_conf.h: LV_MEM_POOL_ALLOC): the start of the block INSTALL took, of INSTALL's heap_bytes.
 * lv_init builds LVGL's TLSF heap on its first LV_MEM_SIZE bytes; sys_lvgl adds the rest; RESET runs lv_deinit and
 * frees it.
 * @param[in] size LV_MEM_SIZE
 * @return The start of the block
 */
void* sys_lvgl_pool_take(size_t size);

/** @brief LV_ASSERT_HANDLER (lv_conf.h): an LVGL assert is a bug. Logs and aborts; never returns. */
void sys_lvgl_assert_failed(void) __attribute__((noreturn));
