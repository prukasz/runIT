#pragma once

#include "errors.h"
#include "sys_render_id.h"

/* Errors of sys_render (MODULE_ID_RENDER): install, the pool and the draw buffers, the display it draws on (`id`). */

#define ERR_SYS_RENDER_MAP(X, M)                                                                                                                                                       \
  X(M, ERR_RENDER_ALREADY_INSTALLED,        1,  ERR_SEV_MEDIUM,   {}) /* install while installed: reset first */                                                                       \
  X(M, ERR_RENDER_NOT_INSTALLED,            2,  ERR_SEV_MEDIUM,   {}) /* a call that needs an installed adapter */                                                                     \
  X(M, ERR_RENDER_PIXEL_FORMAT_UNSUPPORTED, 3,  ERR_SEV_HIGH,     { uint8_t id; uint8_t format; /*@enum-ref sys_display_format_e*/ }) /* a pixel format the adapter does not draw */   \
  X(M, ERR_RENDER_ROW_EXCEEDS_FLUSH_MAX,    4,  ERR_SEV_HIGH,     { uint8_t id; uint32_t flush_max; uint32_t row; }) /* the display's largest FLUSH is less than a row of a buffer */  \
  X(M, ERR_RENDER_ALLOC_FAILED,             5,  ERR_SEV_HIGH,     { uint32_t bytes; uint32_t free; }) /* pool, buffer or display: asked, largest free block (bytes 0: unknown) */      \
  X(M, ERR_RENDER_HEAP_BYTES_OUT_OF_RANGE,  6,  ERR_SEV_MEDIUM,   { uint32_t heap_bytes; uint32_t min; uint32_t max; }) /* the pool outside SYS_RENDER_HEAP_BYTES_MIN .. _MAX */       \
  X(M, ERR_RENDER_POOL_BELOW_RESERVE,       7,  ERR_SEV_MEDIUM,   { uint32_t free; uint32_t need; uint32_t reserve; }) /* it would leave less than the reserve in the engine's pool */ \
  X(M, ERR_RENDER_REFRESH_HZ_OUT_OF_RANGE,  8,  ERR_SEV_MEDIUM,   { uint8_t refresh_hz; uint8_t min; uint8_t max; }) /* outside SYS_RENDER_REFRESH_HZ_MIN .. _MAX */

ERR_TAGS_DECLARE(MODULE_ID_RENDER, ERR_SYS_RENDER_MAP)
