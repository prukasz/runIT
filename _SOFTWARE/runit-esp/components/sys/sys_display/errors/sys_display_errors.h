#pragma once

#include "errors.h"

/* ----------------------------------------------------------------------------
 * Errors (module ERR_MOD_SYS_DISPLAY): what a display driver reports besides the
 * shared ones in sys_device.h (RANGE, SUSPENDED), and the screen adapter's FLUSH timeout.
 * ------------------------------------------------------------------------- */

#define ERR_SYS_DISPLAY_MAP(X)                                                                                                                   \
  X(ERR_DISPLAY_BAD_WINDOW,    ERR_TAG(ERR_MOD_SYS_DISPLAY, 1), ERR_SEV_HIGH, { uint16_t id; uint16_t x1; uint16_t y1; uint16_t x2; uint16_t y2; }) /* the window is empty, outside the panel, or its bytes are not the FLUSH's len */ \
  X(ERR_DISPLAY_FLUSH_BUSY,    ERR_TAG(ERR_MOD_SYS_DISPLAY, 2), ERR_SEV_HIGH, { uint16_t id; })                                                        \
  X(ERR_DISPLAY_TRANSFER,      ERR_TAG(ERR_MOD_SYS_DISPLAY, 3), ERR_SEV_HIGH, { uint16_t id; int32_t code; })                                      /* the bus refused the transfer (from FLUSH at once), or one accepted before failed (from the next FLUSH, once); code: the bus's esp_err_t */ \
  X(ERR_DISPLAY_FLUSH_TIMEOUT, ERR_TAG(ERR_MOD_SYS_DISPLAY, 4), ERR_SEV_HIGH, { uint16_t id; uint16_t ms; })                                         /* the end of an accepted FLUSH did not come within ms: the adapter reports it and resets the display */

ERR_TAGS_DECLARE(ERR_SYS_DISPLAY_MAP)
