#pragma once

/* LVGL's memory (lv_conf.h), SYS_LVGL.MD "The pool". One block for everything LVGL allocates: INSTALL takes heap_bytes,
 * lv_init builds LVGL's TLSF heap on its first LV_MEM_SIZE bytes (LV_MEM_POOL_ALLOC) and sys_lvgl adds the rest as a
 * second pool; RESET runs lv_deinit and frees the block (sys_lvgl_pool.h). Nothing of LVGL in the system heap.
 * LV_MEM_SIZE + LV_MEM_POOL_EXPAND_SIZE is the largest block: CONFIG_SYS_LVGL_HEAP_DEFAULT's range, INSTALL's limit (it
 * sizes TLSF's index). */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
#define LV_MEM_SIZE             (32u * 1024u)                        // = SYS_LVGL_HEAP_MIN
#define LV_MEM_POOL_EXPAND_SIZE (4u * 1024u * 1024u - LV_MEM_SIZE)  // = SYS_LVGL_HEAP_MAX - LV_MEM_SIZE
#define LV_MEM_POOL_INCLUDE     "sys_lvgl_pool.h"
#define LV_MEM_POOL_ALLOC(size) sys_lvgl_pool_take(size)
