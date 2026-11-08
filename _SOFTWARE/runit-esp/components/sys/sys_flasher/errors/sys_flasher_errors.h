#pragma once

#include "errors.h"

#define ERR_SYS_FLASHER_MAP(X)                                                                                         \
  X(ERR_FLASHER_BUSY,      ERR_TAG(ERR_MOD_SYS_FLASHER, 1), ERR_SEV_MEDIUM, {})                            /* a flashing runs */ \
  X(ERR_FLASHER_BOARD_ID,  ERR_TAG(ERR_MOD_SYS_FLASHER, 2), ERR_SEV_MEDIUM, {})                            /* this board has id 0, the new board's */ \
  X(ERR_FLASHER_NO_ANSWER, ERR_TAG(ERR_MOD_SYS_FLASHER, 3), ERR_SEV_HIGH,   { uint8_t op; uint16_t bytes; }) /* the ROM did not answer; bytes: what the line brought */ \
  X(ERR_FLASHER_ROM,       ERR_TAG(ERR_MOD_SYS_FLASHER, 4), ERR_SEV_HIGH,   { uint8_t op; uint8_t code; }) /* the ROM refused a command */ \
  X(ERR_FLASHER_VERIFY,    ERR_TAG(ERR_MOD_SYS_FLASHER, 5), ERR_SEV_HIGH,   { uint32_t addr; })            /* a region's MD5 differs */ \
  X(ERR_FLASHER_IMAGE,     ERR_TAG(ERR_MOD_SYS_FLASHER, 6), ERR_SEV_HIGH,   {})                            /* this board's own firmware can't be read */ \
  X(ERR_FLASHER_ABORTED,   ERR_TAG(ERR_MOD_SYS_FLASHER, 7), ERR_SEV_LOW,    {})                            /* ABORT stopped it */

ERR_TAGS_DECLARE(ERR_SYS_FLASHER_MAP)
