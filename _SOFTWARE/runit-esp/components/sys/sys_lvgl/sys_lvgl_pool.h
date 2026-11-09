#pragma once

#include <stddef.h>

/* LVGL's block (lv_conf.h: LV_MEM_POOL_ALLOC): the start of the block INSTALL took, of INSTALL's heap_bytes. lv_init
 * builds LVGL's TLSF heap on its first LV_MEM_SIZE bytes; sys_lvgl adds the rest; RESET runs lv_deinit and frees it. */
void* sys_lvgl_pool_take(size_t size);
