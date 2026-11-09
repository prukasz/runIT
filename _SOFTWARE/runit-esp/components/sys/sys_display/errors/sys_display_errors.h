#pragma once

#include "errors.h"

/* ----------------------------------------------------------------------------
 * Errors (module ERR_MOD_SYS_DISPLAY): what a display driver reports besides the
 * shared ones in sys_device.h (RANGE, SUSPENDED).
 * ------------------------------------------------------------------------- */

#define ERR_SYS_DISPLAY_MAP(X)                                                                                                                   \
  X(ERR_DISPLAY_BAD_WINDOW, ERR_TAG(ERR_MOD_SYS_DISPLAY, 1), ERR_SEV_HIGH, { uint16_t id; uint16_t x1; uint16_t y1; uint16_t x2; uint16_t y2; }) /* the window is empty, outside the panel or past the buffer */ \
  X(ERR_DISPLAY_FLUSH_BUSY, ERR_TAG(ERR_MOD_SYS_DISPLAY, 2), ERR_SEV_HIGH, { uint16_t id; })                                                        \
  X(ERR_DISPLAY_TRANSFER,   ERR_TAG(ERR_MOD_SYS_DISPLAY, 3), ERR_SEV_HIGH, { uint16_t id; int32_t code; })                                      /* the bus transfer failed; FLUSH_DONE carries its code, the adapter builds this error */ \
  X(ERR_DISPLAY_BAD_BUFFER, ERR_TAG(ERR_MOD_SYS_DISPLAY, 4), ERR_SEV_HIGH, { uint8_t buffer; })                                                 /* the buffer id is past SYS_DISPLAY_BUFFERS, or the buffer is not set */

ERR_TAGS_DECLARE(ERR_SYS_DISPLAY_MAP)
