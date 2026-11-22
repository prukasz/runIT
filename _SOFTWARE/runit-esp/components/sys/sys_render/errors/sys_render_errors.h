/**
 * @file
 * @brief Errors of the sys_render module (MODULE_ID_RENDER): install, the pool and the draw
 * buffers, the display it draws on.
 */
#pragma once

#include "errors.h"
#include "sys_render/sys_render_id.h"

/**
 * @brief Error map of the module. Error numbers are wire format: number 3 was retired and is never
 * used again, the others are never renumbered.
 * @param X Macro applied to each error: X(M, name, code, severity, args struct)
 * @param M The module, passed back to X
 */
#define ERR_SYS_RENDER_MAP(X, M)                                                                   \
    X(M, ERR_RENDER_ALREADY_INSTALLED, 1, ERR_SEV_MEDIUM,                                          \
      {}) /* install while installed: reset first */                                               \
    X(M, ERR_RENDER_NOT_INSTALLED, 2, ERR_SEV_MEDIUM,                                              \
      {}) /* a call that needs an installed adapter */                                             \
    X(M, ERR_RENDER_PIXEL_FORMAT_UNSUPPORTED, 4, ERR_SEV_HIGH, {                                   \
        uint8_t id;                                                                                \
        uint8_t format;                                                                            \
    }) /* a pixel format the adapter does not draw */                                              \
    X(M, ERR_RENDER_ROW_EXCEEDS_FLUSH_MAX, 5, ERR_SEV_HIGH, {                                      \
        uint8_t id;                                                                                \
        uint32_t flush_max;                                                                        \
        uint32_t row;                                                                              \
    }) /* the display's largest FLUSH is less than one row of a draw buffer */                     \
    X(M, ERR_RENDER_ALLOC_FAILED, 6, ERR_SEV_HIGH, {                                               \
        uint32_t bytes;                                                                            \
        uint32_t free;                                                                             \
    }) /* no memory for the pool, a draw buffer or the display: what was to be taken and the       \
          largest free block (bytes 0: the engine does not say) */                                 \
    X(M, ERR_RENDER_HEAP_BYTES_OUT_OF_RANGE, 7, ERR_SEV_MEDIUM, {                                  \
        uint32_t heap_bytes;                                                                       \
        uint32_t min;                                                                              \
        uint32_t max;                                                                              \
    }) /* the pool asked for is outside SYS_RENDER_HEAP_BYTES_MIN .. _MAX */                       \
    X(M, ERR_RENDER_POOL_BELOW_RESERVE, 8, ERR_SEV_MEDIUM, {                                       \
        uint32_t free;                                                                             \
        uint32_t need;                                                                             \
        uint32_t reserve;                                                                          \
    }) /* what was asked would leave less than the reserve in the engine's pool */                 \
    X(M, ERR_RENDER_REFRESH_HZ_OUT_OF_RANGE, 9, ERR_SEV_MEDIUM, {                                  \
        uint8_t refresh_hz;                                                                        \
        uint8_t min;                                                                               \
        uint8_t max;                                                                               \
    }) /* the refresh rate asked for is outside SYS_RENDER_REFRESH_HZ_MIN .. _MAX */

ERR_TAGS_DECLARE(MODULE_ID_RENDER, ERR_SYS_RENDER_MAP)
