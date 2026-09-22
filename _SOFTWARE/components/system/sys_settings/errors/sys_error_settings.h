#pragma once
#include <stdint.h>
#include <stdio.h>

// Error owners and tags for sys_settings. sys_errors aggregates this map through
// its include dirs (sys_error_codes.h). This folder holds only error maps.
#define SYS_SETTINGS_OWNER_MAP(X)                                  \
  X(OWNER_SYS_SETTINGS_BASE, 0xB000, "OWNER_SYS_SETTINGS_BASE")   \
  X(OWNER_SYS_SETTINGS_INIT, 0xB001, "OWNER_SYS_SETTINGS_INIT")   \
  X(OWNER_SYS_SETTINGS_LOAD, 0xB002, "OWNER_SYS_SETTINGS_LOAD")   \
  X(OWNER_SYS_SETTINGS_STORE, 0xB003, "OWNER_SYS_SETTINGS_STORE") \
  X(OWNER_SYS_SETTINGS_ERASE, 0xB004, "OWNER_SYS_SETTINGS_ERASE")

#define SYS_ERROR_SETTINGS_MAP(X) \
  X(ERR_SETTINGS_SIZE_MISMATCH, 0xB001, SE_LEVEL_LOW, struct { uint16_t stored; uint16_t expected; })

#define SYS_ERROR_SETTINGS_LOGGER_MAP(X) X(ERR_SETTINGS_SIZE_MISMATCH)

#define LOG_BODY_ERR_SETTINGS_SIZE_MISMATCH(p, out, out_size) \
  snprintf((out), (out_size), "stored setting is %u bytes, expected %u (layout changed; defaults used)", (p)->stored, (p)->expected)
