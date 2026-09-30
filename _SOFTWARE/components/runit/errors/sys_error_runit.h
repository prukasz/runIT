#pragma once

// Error owners for runit (the application). sys_errors aggregates this map through its
// include dirs (sys_error_codes.h). This folder holds only error maps, so it
// doesn't expose the module's API to everything that includes sys_error.h.
#define RUNIT_OWNER_MAP(X) \
  X(OWNER_RUNIT_BASE, 0xAF00, "OWNER_RUNIT_BASE")                 \
  X(OWNER_RUNIT_BOARD, 0xAF01, "OWNER_RUNIT_BOARD")               \
  X(OWNER_RUNIT_ERROR_POLICY, 0xAF02, "OWNER_RUNIT_ERROR_POLICY")
