#pragma once

#include "errors.h"

#define ERR_SYS_ACTIONS_MAP(X)                                                                                   \
  X(ERR_ACTION_NOT_FOUND,  ERR_TAG(ERR_MOD_SYS_ACTIONS, 1),  ERR_SEV_MEDIUM, { uint32_t id; })                   \
  X(ERR_ACTION_BUSY,       ERR_TAG(ERR_MOD_SYS_ACTIONS, 3),  ERR_SEV_MEDIUM, {}) /* a recording or upload is open */ \
  X(ERR_ACTION_NO_SESSION, ERR_TAG(ERR_MOD_SYS_ACTIONS, 4),  ERR_SEV_MEDIUM, {}) /* no recording / upload of that kind open */ \
  X(ERR_ACTION_TOO_BIG,    ERR_TAG(ERR_MOD_SYS_ACTIONS, 5),  ERR_SEV_MEDIUM, { uint16_t len; uint16_t max; }) \
  X(ERR_ACTION_OFFSET,     ERR_TAG(ERR_MOD_SYS_ACTIONS, 6),  ERR_SEV_MEDIUM, { uint16_t got; uint16_t want; }) /* upload write out of order, read past the image */ \
  X(ERR_ACTION_INCOMPLETE, ERR_TAG(ERR_MOD_SYS_ACTIONS, 7),  ERR_SEV_MEDIUM, { uint16_t got; uint16_t want; }) /* commit before every byte came */ \
  X(ERR_ACTION_CRC,        ERR_TAG(ERR_MOD_SYS_ACTIONS, 8),  ERR_SEV_MEDIUM, { uint32_t got; uint32_t want; }) \
  X(ERR_ACTION_BAD_FRAME,  ERR_TAG(ERR_MOD_SYS_ACTIONS, 9),  ERR_SEV_MEDIUM, { uint16_t offset; }) /* image framing broken there */ \
  X(ERR_ACTION_EMPTY,      ERR_TAG(ERR_MOD_SYS_ACTIONS, 10), ERR_SEV_LOW,    {}) /* recording stopped with no frame */ \
  X(ERR_ACTION_DEPTH,      ERR_TAG(ERR_MOD_SYS_ACTIONS, 11), ERR_SEV_MEDIUM, { uint8_t depth; }) /* RUN nested deeper than CONFIG_SYS_ACTIONS_MAX_DEPTH */ \
  X(ERR_ACTION_WRONG_TASK, ERR_TAG(ERR_MOD_SYS_ACTIONS, 12), ERR_SEV_HIGH,   {}) /* sys_actions_run outside a router task */ \
  X(ERR_ACTION_NO_MEM,     ERR_TAG(ERR_MOD_SYS_ACTIONS, 14), ERR_SEV_HIGH,   { uint32_t size; }) \
  X(ERR_ACTION_STEP,       ERR_TAG(ERR_MOD_SYS_ACTIONS, 15), ERR_SEV_NONE,   { uint32_t id; uint16_t step; }) /* context: frame `step` (from 0) failed */ \
  X(ERR_ACTION_BAD_ARG,    ERR_TAG(ERR_MOD_SYS_ACTIONS, 16), ERR_SEV_HIGH,   {}) \
  X(ERR_ACTION_BAD_ID,     ERR_TAG(ERR_MOD_SYS_ACTIONS, 17), ERR_SEV_HIGH,   {}) /* ID 0 (SYS_ACTIONS_ID_NONE) */

ERR_TAGS_DECLARE(ERR_SYS_ACTIONS_MAP)
