#pragma once

#include "errors.h"
#include "sys_display_id.h"

/* Errors of sys_display_contract (MODULE_ID_DISPLAY): what a display driver reports besides the core ones (ERR_RANGE,
 * ERR_BAD_ARG) and the registry's (ERR_DEV_SUSPENDED), and the screen adapter's FLUSH timeout. `id` is the display's
 * device. A driver on sys_spi_contract passes the bus's own error for a failed transfer (ERR_SPI_XFER, ERR_SPI_QUEUE_FULL). */

#define ERR_SYS_DISPLAY_MAP(X, M)                                                                                                                                                                            \
  X(M, ERR_DISPLAY_BAD_WINDOW,    1,  ERR_SEV_HIGH,     { uint8_t id; uint16_t x1; uint16_t y1; uint16_t x2; uint16_t y2; }) /* empty, outside the panel, not the FLUSH's len, or len above flush_max */     \
  X(M, ERR_DISPLAY_FLUSH_BUSY,    2,  ERR_SEV_HIGH,     { uint8_t id; }) /* a FLUSH while one runs */                                                                                                        \
  X(M, ERR_DISPLAY_TRANSFER,      3,  ERR_SEV_HIGH,     { uint8_t id; esp_err_t code; }) /* the bus refused the transfer: its esp_err_t */                                                                   \
  X(M, ERR_DISPLAY_FLUSH_TIMEOUT, 4,  ERR_SEV_HIGH,     { uint8_t id; uint16_t timeout_ms; }) /* an accepted FLUSH did not end: the adapter resets the display */                                            \
  X(M, ERR_DISPLAY_LOST,          5,  ERR_SEV_HIGH,     { uint8_t id; uint8_t health; /*@enum-ref sys_display_health_e*/ uint8_t power_mode; }) /* HEALTH: no answer or power lost; once until it answers */

ERR_TAGS_DECLARE(MODULE_ID_DISPLAY, ERR_SYS_DISPLAY_MAP)
