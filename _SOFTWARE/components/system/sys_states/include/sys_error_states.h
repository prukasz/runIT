#pragma once
#include <stdint.h>

#define SYS_STATES_OWNER_MAP(X) \
  X(OWNER_SYS_STATES_BASE, 0xA900, "OWNER_SYS_STATES_BASE")

#define SYS_ERROR_STATES_MAP(X)                    \
  X(ERR_STATE_UNKNOWN, struct { uint8_t state; })  \
  X(ERR_STATE_NO_CUSTOM_SLOTS, struct { uint8_t unused; })
