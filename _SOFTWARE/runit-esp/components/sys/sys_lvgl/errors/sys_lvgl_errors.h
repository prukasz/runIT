#pragma once

#include "errors.h"
#include "sys_lvgl_id.h"

/* ----------------------------------------------------------------------------
 * Errors (module MODULE_ID_LVGL): the LVGL adapter's INSTALL and state.
 * ------------------------------------------------------------------------- */

#define ERR_SYS_LVGL_MAP(X, M)                                                                                                                                                \
  X(M, ERR_LVGL_INSTALLED,     1,  ERR_SEV_MEDIUM, {})                                                                                                                        \
  X(M, ERR_LVGL_NOT_INSTALLED, 2,  ERR_SEV_MEDIUM, {})                                                                                                                        \
  X(M, ERR_LVGL_NOT_DISPLAY,   3,  ERR_SEV_HIGH,   { uint8_t id; }) /* around the device's error: GEOMETRY or ORIENTATION failed */                                           \
  X(M, ERR_LVGL_NOT_TOUCH,     4,  ERR_SEV_HIGH,   { uint8_t id; }) /* around the device's error: POINT failed */                                                             \
  X(M, ERR_LVGL_FORMAT,        5,  ERR_SEV_HIGH,   { uint8_t id; uint8_t format; }) /* a pixel format the adapter does not draw */                                            \
  X(M, ERR_LVGL_FLUSH_MAX,     6,  ERR_SEV_HIGH,   { uint8_t id; uint32_t flush_max; uint32_t row; }) /* the display's largest FLUSH is less than one row */                  \
  X(M, ERR_LVGL_NO_MEMORY,     7,  ERR_SEV_HIGH,   { uint32_t bytes; }) /* no memory for LVGL's pool or a draw buffer (DMA), or LVGL could not make the display */            \
  X(M, ERR_LVGL_HEAP,          8,  ERR_SEV_MEDIUM, { uint32_t heap_bytes; uint32_t min; uint32_t max; }) /* the pool asked for is outside SYS_LVGL_HEAP_MIN .. _MAX */        \
  X(M, ERR_LVGL_POOL_FULL,     9,  ERR_SEV_MEDIUM, { uint32_t free; uint32_t need; uint32_t reserve; }) /* what was asked would leave less than the reserve in LVGL's pool */ \
  X(M, ERR_LVGL_REFRESH,       10, ERR_SEV_MEDIUM, { uint8_t refresh_hz; uint8_t min; uint8_t max; }) /* the refresh rate asked for is outside SYS_LVGL_REFRESH_MIN .. _MAX */

ERR_TAGS_DECLARE(MODULE_ID_LVGL, ERR_SYS_LVGL_MAP)
