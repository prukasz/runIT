#pragma once

#include "errors.h"
#include "sys_actions_id.h"

// Codes 2 and 13 are unused: never assign them.
#define ERR_SYS_ACTIONS_MAP(X, M)                                                                                                        \
  X(M, ERR_ACTION_NOT_FOUND,  1,  ERR_SEV_MEDIUM, { uint32_t id; })                                                                      \
  X(M, ERR_ACTION_BUSY,       2,  ERR_SEV_MEDIUM, {}) /* a record is being written */                                                    \
  X(M, ERR_ACTION_NO_DRAFT,   3,  ERR_SEV_MEDIUM, {}) /* no record is being written */                                                   \
  X(M, ERR_ACTION_TOO_BIG,    4,  ERR_SEV_MEDIUM, { uint16_t len; uint16_t max; })                                                       \
  X(M, ERR_ACTION_OFFSET,     5,  ERR_SEV_MEDIUM, { uint16_t got; uint16_t want; }) /* upload write out of order, read past the image */ \
  X(M, ERR_ACTION_INCOMPLETE, 6,  ERR_SEV_MEDIUM, { uint16_t got; uint16_t want; }) /* commit before every byte came */                  \
  X(M, ERR_ACTION_CRC,        7,  ERR_SEV_MEDIUM, { uint32_t got; uint32_t want; })                                                      \
  X(M, ERR_ACTION_BAD_FRAME,  8,  ERR_SEV_MEDIUM, { uint16_t offset; }) /* image framing broken there */                                 \
  X(M, ERR_ACTION_EMPTY,      9,  ERR_SEV_LOW,    {}) /* a record with no frame */                                                       \
  X(M, ERR_ACTION_DEPTH,      10, ERR_SEV_MEDIUM, { uint8_t depth; }) /* RUN nested deeper than CONFIG_SYS_ACTIONS_MAX_DEPTH */          \
  X(M, ERR_ACTION_WRONG_TASK, 11, ERR_SEV_HIGH,   {}) /* sys_actions_run outside a router task */                                        \
  X(M, ERR_ACTION_NO_MEM,     12, ERR_SEV_HIGH,   { uint32_t size; })                                                                    \
  X(M, ERR_ACTION_STEP,       13, ERR_SEV_NONE,   { uint32_t id; uint16_t step; }) /* context: frame `step` (from 0) failed */           \
  X(M, ERR_ACTION_BAD_ARG,    14, ERR_SEV_HIGH,   {})                                                                                    \
  X(M, ERR_ACTION_BAD_ID,     15, ERR_SEV_HIGH,   {}) /* ID 0 (SYS_ACTIONS_ID_NONE) */

ERR_TAGS_DECLARE(MODULE_ID_ACTIONS, ERR_SYS_ACTIONS_MAP)
