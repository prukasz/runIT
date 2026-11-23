/**
 * @file
 * @brief Errors of the sys_display module (MODULE_ID_DISPLAY): window, busy FLUSH, transfer,
 *        timeout, lost panel.
 */
#pragma once

#include "errors.h"
#include "sys_display/sys_display_id.h"

/* ----------------------------------------------------------------------------
 * Errors (module MODULE_ID_DISPLAY): what a display driver reports besides the core ones in
 * errors.h (RANGE, BAD_ARG) and sys_device.h (SUSPENDED), and the screen adapter's FLUSH timeout.
 * ------------------------------------------------------------------------- */

/**
 * @brief Error map of the module
 * @param X Macro applied to each error: X(M, name, code, severity, args struct)
 * @param M The module, passed back to X
 */
#define ERR_SYS_DISPLAY_MAP(X, M)                                                                  \
    X(M, ERR_DISPLAY_BAD_WINDOW, 1, ERR_SEV_HIGH, {                                                \
        uint8_t id;                                                                                \
        uint16_t x1;                                                                               \
        uint16_t y1;                                                                               \
        uint16_t x2;                                                                               \
        uint16_t y2;                                                                               \
    }) /* the window is empty, outside the panel, its bytes are not the FLUSH's len, or len is     \
          above flush_max */                                                                       \
    X(M, ERR_DISPLAY_FLUSH_BUSY, 2, ERR_SEV_HIGH, { uint8_t id; })                                 \
    X(M, ERR_DISPLAY_TRANSFER, 3, ERR_SEV_HIGH, {                                                  \
        uint8_t id;                                                                                \
        esp_err_t code;                                                                            \
    }) /* the bus refused the transfer (from FLUSH at once); code: the bus's esp_err_t. A driver   \
       on sys_spi passes the bus's own error instead (ERR_SPI_XFER, ERR_SPI_QUEUE_FULL), which has \
       no esp_err_t */                                                                             \
    X(M, ERR_DISPLAY_FLUSH_TIMEOUT, 4, ERR_SEV_HIGH, {                                             \
        uint8_t id;                                                                                \
        uint16_t timeout_ms;                                                                       \
    }) /* the end of an accepted FLUSH did not come within timeout_ms: the adapter reports it and  \
          resets the display */                                                                    \
    X(M, ERR_DISPLAY_LOST, 5, ERR_SEV_HIGH, {                                                      \
        uint8_t id;                                                                                \
        uint8_t health;                                                                            \
        uint8_t power_mode;                                                                        \
    }) /* HEALTH: the panel does not answer (health NO_ANSWER) or lost its power (RESTARTED: the   \
          adapter resets it); reported once until it answers again */

ERR_TAGS_DECLARE(MODULE_ID_DISPLAY, ERR_SYS_DISPLAY_MAP)
