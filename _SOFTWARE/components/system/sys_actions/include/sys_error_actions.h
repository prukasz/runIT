#pragma once
#include <stdint.h>

#define SYS_ACTIONS_OWNER_MAP(X) \
  X(OWNER_SYS_ACTIONS_BASE, 0xAA00, "OWNER_SYS_ACTIONS_BASE")

#define SYS_ERROR_ACTIONS_MAP(X)                                                                       \
  X(ERR_ACTION_NOT_FOUND, struct { uint8_t action_id; })                                              \
  X(ERR_ACTION_BLOCK_FULL, struct { uint8_t action_id; uint32_t used; uint32_t size; })                \
  X(ERR_ACTION_STATE_BINDINGS_FULL, struct { uint8_t action_id; uint8_t state; })
