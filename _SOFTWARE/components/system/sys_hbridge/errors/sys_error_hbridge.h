#pragma once

// Error owners for sys_hbridge. sys_errors aggregates this map through its
// include dirs (sys_error_codes.h). This folder holds only error maps, so it
// doesn't expose the module's API to everything that includes sys_error.h.
#define SYS_HBRIDGE_OWNER_MAP(X) \
  X(OWNER_SYS_HBRIDGE_BASE, 0xAC00, "OWNER_SYS_HBRIDGE_BASE")                           \
  X(OWNER_SYS_HBRIDGE_SET_MODE, 0xAC01, "OWNER_SYS_HBRIDGE_SET_MODE")                   \
  X(OWNER_SYS_HBRIDGE_SET_DRIVE, 0xAC02, "OWNER_SYS_HBRIDGE_SET_DRIVE")                 \
  X(OWNER_SYS_HBRIDGE_BRAKE, 0xAC03, "OWNER_SYS_HBRIDGE_BRAKE")                         \
  X(OWNER_SYS_HBRIDGE_COAST, 0xAC04, "OWNER_SYS_HBRIDGE_COAST")                         \
  X(OWNER_SYS_HBRIDGE_GET_CURRENT, 0xAC05, "OWNER_SYS_HBRIDGE_GET_CURRENT")             \
  X(OWNER_SYS_HBRIDGE_SET_CURRENT_LIMIT, 0xAC06, "OWNER_SYS_HBRIDGE_SET_CURRENT_LIMIT") \
  X(OWNER_SYS_HBRIDGE_GET_FAULT, 0xAC08, "OWNER_SYS_HBRIDGE_GET_FAULT")                 \
  X(OWNER_SYS_HBRIDGE_CLEAR_FAULT, 0xAC09, "OWNER_SYS_HBRIDGE_CLEAR_FAULT")
