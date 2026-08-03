#pragma once
#include <stdint.h>
#include <stdio.h>

#define SYS_DEVICE_OWNER_MAP(X)                                           \
  X(OWNER_SYS_DEVICE_BASE, 0xA100, "DEVICE_BASE")                         \
  X(OWNER_SYS_DEVICE_INSTALL, 0xA101, "OWNER_SYS_DEVICE_INSTALL")         \
  X(OWNER_SYS_DEVICE_UNINSTALL, 0xA102, "OWNER_SYS_DEVICE_UNINSTALL")     \
  X(OWNER_SYS_DEVICE_RESET, 0xA103, "OWNER_SYS_DEVICE_RESET")             \
  X(OWNER_SYS_DEVICE_GET_BY_ID, 0xA104, "OWNER_SYS_DEVICE_GET_BY_ID")     \
  X(OWNER_SYS_DEVICE_SUSPEND, 0xA105, "OWNER_SYS_DEVICE_SUSPEND")         \
  X(OWNER_SYS_DEVICE_RESUME, 0xA106, "OWNER_SYS_DEVICE_RESUME")           \
  X(OWNER_SYS_DEVICE_SUSPEND_ALL, 0xA107, "OWNER_SYS_DEVICE_SUSPEND_ALL") \
  X(OWNER_SYS_DEVICE_RESUME_ALL, 0xA108, "OWNER_SYS_DEVICE_RESUME_ALL")   \
  X(OWNER_SYS_DEVICE_FREEZE, 0xA109, "OWNER_SYS_DEVICE_FREEZE")           \
  X(OWNER_SYS_DEVICE_SYNC, 0xA10A, "OWNER_SYS_DEVICE_SYNC")               \
  X(OWNER_SYS_DEVICE_FREEZE_ALL, 0xA10B, "OWNER_SYS_DEVICE_FREEZE_ALL")   \
  X(OWNER_SYS_DEVICE_SYNC_ALL, 0xA10C, "OWNER_SYS_DEVICE_SYNC_ALL")       \
  X(OWNER_SYS_DEVICE_RESET_ALL, 0xA10D, "OWNER_SYS_DEVICE_RESET_ALL")     \
  X(OWNER_SYS_DEVICE_UNINSTALL_ALL, 0xA10E, "OWNER_SYS_DEVICE_UNINSTALL_ALL") \
  X(OWNER_SYS_DEVICE_REPORT_ERROR, 0xA10F, "OWNER_SYS_DEVICE_REPORT_ERROR")   \
  X(OWNER_SYS_DEVICE_SET_ERROR_HANDLING, 0xA110, "OWNER_SYS_DEVICE_SET_ERROR_HANDLING")

#define SYS_ERROR_DEV_MAP(X) \
    X(ERR_DEV_NO_HANDLE, struct { uint8_t dev_id; }) \
    X(ERR_DEV_NOT_FOUND, struct { uint8_t dev_id; }) \
    X(ERR_DEV_ALREADY_EXIST, struct { uint8_t dev_id; }) \
    X(ERR_DEV_FEATURE_UNAVAILABLE, struct { uint8_t dev_id; uint8_t contract_id; uint8_t feature_id; }) \
    X(ERR_DEV_SUSPENDED, struct { uint8_t dev_id; }) \
    X(ERR_DEV_NOT_INSTALLED, struct { uint8_t dev_id; }) \
    X(ERR_DEV_INSTALL_FAILED, struct { uint8_t dev_id; })

/**
 * @brief Human-readable descriptions for the sys_device tags - see
 * SE_describe_payload() in sys_error.h and sys_error_base.h's LOGGER_MAP
 * comment for why these are text-only LOG_BODY_* macros rather than typed
 * functions (the payload struct types don't exist yet at this point in the
 * include chain).
 *
 * ERR_DEV_FEATURE_UNAVAILABLE translates both contract_id and feature_id to
 * their real names via the two extern string tables forward-declared below
 * - NOT via `#include "sys_device.h"` / `#include "sys_io.h"`, which would
 * break: those headers need err_h (from sys_error.h), which isn't defined
 * yet at this point in the include chain (sys_error_codes.h, which pulls
 * this file in, is included by sys_error.h *before* err_h's typedef).
 * Forward-declaring just the one symbol each side actually needs sidesteps
 * that without a real #include.
 */
extern const char* const sys_device_contract_type_e_to_string[];  // sys_device.h/.c - 4 entries
extern const char* const sys_io_feature_e_to_string[];            // sys_io.h/.c - 10 entries, only valid when contract_id == 0 (SYS_DEVICE_CONTRACT_IO)

#define SYS_ERROR_DEV_LOGGER_MAP(X)  \
  X(ERR_DEV_NO_HANDLE)               \
  X(ERR_DEV_NOT_FOUND)               \
  X(ERR_DEV_ALREADY_EXIST)           \
  X(ERR_DEV_FEATURE_UNAVAILABLE)     \
  X(ERR_DEV_SUSPENDED)               \
  X(ERR_DEV_NOT_INSTALLED)           \
  X(ERR_DEV_INSTALL_FAILED)

#define LOG_BODY_ERR_DEV_NO_HANDLE(p, out, out_size) snprintf((out), (out_size), "device %u has no handle (installed but handle is NULL)", (p)->dev_id)
#define LOG_BODY_ERR_DEV_NOT_FOUND(p, out, out_size) snprintf((out), (out_size), "device %u is not registered", (p)->dev_id)
#define LOG_BODY_ERR_DEV_ALREADY_EXIST(p, out, out_size) snprintf((out), (out_size), "device %u is already registered", (p)->dev_id)
#define LOG_BODY_ERR_DEV_FEATURE_UNAVAILABLE(p, out, out_size)                                                                                  \
  do {                                                                                                                                          \
    const char* __contract = (p)->contract_id < 4 ? sys_device_contract_type_e_to_string[(p)->contract_id] : "UNKNOWN";                        \
    if ((p)->contract_id == 0 && (p)->feature_id < 10) {                                                                                        \
      snprintf((out), (out_size), "device %u: feature %s unavailable on contract %s", (p)->dev_id, sys_io_feature_e_to_string[(p)->feature_id], __contract); \
    } else {                                                                                                                                    \
      snprintf((out), (out_size), "device %u: feature %u unavailable on contract %s", (p)->dev_id, (p)->feature_id, __contract);               \
    }                                                                                                                                            \
  } while (0)
#define LOG_BODY_ERR_DEV_SUSPENDED(p, out, out_size) snprintf((out), (out_size), "device %u is suspended", (p)->dev_id)
#define LOG_BODY_ERR_DEV_NOT_INSTALLED(p, out, out_size) snprintf((out), (out_size), "device %u is registered but not installed", (p)->dev_id)
#define LOG_BODY_ERR_DEV_INSTALL_FAILED(p, out, out_size) snprintf((out), (out_size), "device %u failed to install", (p)->dev_id)
