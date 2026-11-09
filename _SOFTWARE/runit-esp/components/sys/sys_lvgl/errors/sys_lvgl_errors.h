#pragma once

#include "errors.h"

/* ----------------------------------------------------------------------------
 * Errors (module ERR_MOD_SYS_LVGL): the LVGL adapter's INSTALL and state.
 * ------------------------------------------------------------------------- */

#define ERR_SYS_LVGL_MAP(X)                                                                                                         \
  X(ERR_LVGL_INSTALLED,     ERR_TAG(ERR_MOD_SYS_LVGL, 1), ERR_SEV_MEDIUM, {})                                                     \
  X(ERR_LVGL_NOT_INSTALLED, ERR_TAG(ERR_MOD_SYS_LVGL, 2), ERR_SEV_MEDIUM, {})                                                     \
  X(ERR_LVGL_NOT_DISPLAY,   ERR_TAG(ERR_MOD_SYS_LVGL, 3), ERR_SEV_HIGH,   { uint16_t id; })                                       /* around the device's error: GEOMETRY or ORIENTATION failed */ \
  X(ERR_LVGL_NOT_TOUCH,     ERR_TAG(ERR_MOD_SYS_LVGL, 4), ERR_SEV_HIGH,   { uint16_t id; })                                       /* around the device's error: POINT failed */ \
  X(ERR_LVGL_FORMAT,        ERR_TAG(ERR_MOD_SYS_LVGL, 5), ERR_SEV_HIGH,   { uint16_t id; uint8_t format; })                       /* a pixel format the adapter does not draw */ \
  X(ERR_LVGL_FLUSH_MAX,     ERR_TAG(ERR_MOD_SYS_LVGL, 6), ERR_SEV_HIGH,   { uint16_t id; uint32_t flush_max; uint32_t row; })     /* the display's largest FLUSH is less than one row */ \
  X(ERR_LVGL_NO_MEMORY,     ERR_TAG(ERR_MOD_SYS_LVGL, 7), ERR_SEV_HIGH,   { uint32_t bytes; })                                    /* no memory for LVGL's pool or a draw buffer (DMA), or LVGL could not make the display */ \
  X(ERR_LVGL_HEAP,          ERR_TAG(ERR_MOD_SYS_LVGL, 8), ERR_SEV_MEDIUM, { uint32_t heap_bytes; uint32_t min; uint32_t max; })  /* the pool asked for is outside SYS_LVGL_HEAP_MIN .. _MAX */   X(ERR_LVGL_POOL_FULL,     ERR_TAG(ERR_MOD_SYS_LVGL, 9), ERR_SEV_MEDIUM, { uint32_t free; uint32_t need; uint32_t reserve; })     /* what was asked would leave less than the reserve in LVGL's pool */

ERR_TAGS_DECLARE(ERR_SYS_LVGL_MAP)
