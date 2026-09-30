#pragma once

// Error owners for features. sys_errors aggregates this map through its
// include dirs (sys_error_codes.h). This folder holds only error maps, so it
// doesn't expose the module's API to everything that includes sys_error.h.
#define FEATURES_OWNER_MAP(X) \
  X(OWNER_FEATURES_BASE, 0xAE00, "OWNER_FEATURES_BASE")         \
  X(OWNER_FEATURES_REGISTRY, 0xAE01, "OWNER_FEATURES_REGISTRY") \
  X(OWNER_FEATURES_SERVO, 0xAE02, "OWNER_FEATURES_SERVO")       \
  X(OWNER_FEATURES_HBRIDGE, 0xAE03, "OWNER_FEATURES_HBRIDGE")
