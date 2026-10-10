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
  X(OWNER_SYS_DEVICE_RESET_ALL, 0xA10D, "OWNER_SYS_DEVICE_RESET_ALL")     \
  X(OWNER_SYS_DEVICE_UNINSTALL_ALL, 0xA10E, "OWNER_SYS_DEVICE_UNINSTALL_ALL") \
  X(OWNER_SYS_DEVICE_REPORT_ERROR, 0xA10F, "OWNER_SYS_DEVICE_REPORT_ERROR")   \
  X(OWNER_SYS_DEVICE_SET_ERROR_HANDLING, 0xA110, "OWNER_SYS_DEVICE_SET_ERROR_HANDLING") \
  X(OWNER_SYS_DEVICE_USER_UNINSTALL, 0xA112, "OWNER_SYS_DEVICE_USER_UNINSTALL")       \
  X(OWNER_SYS_DEVICE_USER_UNINSTALL_ALL, 0xA113, "OWNER_SYS_DEVICE_USER_UNINSTALL_ALL") \
  X(OWNER_SYS_DEVICE_ROUTE, 0xA114, "OWNER_SYS_DEVICE_ROUTE")                           \
  X(OWNER_SYS_DEVICE_REGISTER_CLASS, 0xA115, "OWNER_SYS_DEVICE_REGISTER_CLASS")         \
  X(OWNER_SYS_DEVICE_REPLY_PUT, 0xA116, "OWNER_SYS_DEVICE_REPLY_PUT")               \
  X(OWNER_SYS_DEVICE_REGISTER_CONTRACT, 0xA117, "OWNER_SYS_DEVICE_REGISTER_CONTRACT") \
  X(OWNER_SYS_DEVICE_CREATE, 0xA118, "OWNER_SYS_DEVICE_CREATE") \
  X(OWNER_SYS_DEVICE_CHECK, 0xA119, "OWNER_SYS_DEVICE_CHECK") \
  X(OWNER_SYS_DEVICE_CREATE_ONBOARD, 0xA11A, "OWNER_SYS_DEVICE_CREATE_ONBOARD")

#define SYS_ERROR_DEV_MAP(X) \
    X(ERR_DEV_NOT_FOUND, 0xA102, SE_LEVEL_LOW, struct { uint8_t dev_id; /*@id device*/ }) \
    X(ERR_DEV_ALREADY_EXIST, 0xA103, SE_LEVEL_LOW, struct { uint8_t dev_id; /*@id device*/ }) \
    X(ERR_DEV_FEATURE_UNAVAILABLE, 0xA104, SE_LEVEL_LOW, struct { uint8_t dev_id; /*@id device*/ uint8_t contract_id; /*@enum-ref sys_device_contract_type_e*/ uint8_t feature_id; /*@id contract-feature contract_id*/ }) \
    X(ERR_DEV_SUSPENDED, 0xA105, SE_LEVEL_MEDIUM, struct { uint8_t dev_id; /*@id device*/ }) \
    X(ERR_DEV_NOT_INSTALLED, 0xA106, SE_LEVEL_LOW, struct { uint8_t dev_id; /*@id device*/ }) \
    X(ERR_DEV_INSTALL_FAILED, 0xA107, SE_LEVEL_HIGH, struct { uint8_t dev_id; /*@id device*/ }) \
    X(ERR_DEV_FAULT_RESPONSE_FAILED, 0xA108, SE_LEVEL_CRITICAL, struct { uint8_t dev_id; /*@id device*/ uint8_t level; /*@id error-level*/ uint8_t stage; uint8_t action_id; uint16_t cause_tag; /*@id error-tag*/ }) \
    X(ERR_DEV_INSTALL_STEP_FAILED, 0xA10A, SE_LEVEL_HIGH, struct { uint16_t line; }) \
    X(ERR_DEV_ONBOARD, 0xA10B, SE_LEVEL_LOW, struct { uint8_t dev_id; /*@id device*/ }) \
    X(ERR_DEV_PIN_ORDER, 0xA10C, SE_LEVEL_LOW, struct { uint8_t dev_id; /*@id device*/ uint8_t pin_dev_id; /*@id device*/ uint8_t pin; }) \
    X(ERR_DEV_OP_UNKNOWN, 0xA10D, SE_LEVEL_LOW, struct { uint8_t dev_id; /*@id device*/ uint8_t op; }) \
    X(ERR_DEV_OP_SIZE, 0xA10E, SE_LEVEL_LOW, struct { uint8_t dev_id; /*@id device*/ uint8_t op; uint16_t got; uint16_t need; }) \
    X(ERR_DEV_TYPE_UNKNOWN, 0xA10F, SE_LEVEL_LOW, struct { uint8_t type_id; }) \
    X(ERR_DEV_CREATE_SIZE, 0xA110, SE_LEVEL_LOW, struct { uint8_t type_id; uint16_t got; uint16_t need; })

/**
 * @brief Human-readable descriptions for the sys_device tags - see
 * SE_describe_payload() in sys_error.h and sys_error_base.h's LOGGER_MAP
 * comment for why these are text-only LOG_BODY_* macros rather than typed
 * functions (the payload struct types don't exist yet at this point in the
 * include chain).
 *
 * ERR_DEV_FEATURE_UNAVAILABLE translates both contract_id and feature_id to
 * their real names via the two functions forward-declared below (sys_device.c,
 * filled by the contracts at boot, sys_device_register_contract) - NOT via
 * `#include "sys_device.h"`, which would break: those headers need err_h (from sys_error.h), which isn't defined
 * yet at this point in the include chain (sys_error_codes.h, which pulls
 * this file in, is included by sys_error.h *before* err_h's typedef).
 * Forward-declaring just the one symbol each side actually needs sidesteps
 * that without a real #include.
 */
const char* sys_device_contract_name(uint8_t contract_id);                     // sys_device.c
const char* sys_device_feature_name(uint8_t contract_id, uint8_t feature_id);  // sys_device.c

#define SYS_ERROR_DEV_LOGGER_MAP(X)  \
  X(ERR_DEV_NOT_FOUND)               \
  X(ERR_DEV_ALREADY_EXIST)           \
  X(ERR_DEV_FEATURE_UNAVAILABLE)     \
  X(ERR_DEV_SUSPENDED)               \
  X(ERR_DEV_NOT_INSTALLED)           \
  X(ERR_DEV_INSTALL_FAILED)          \
  X(ERR_DEV_FAULT_RESPONSE_FAILED)   \
  X(ERR_DEV_INSTALL_STEP_FAILED)     \
  X(ERR_DEV_ONBOARD)                 \
  X(ERR_DEV_PIN_ORDER)               \
  X(ERR_DEV_OP_UNKNOWN)              \
  X(ERR_DEV_OP_SIZE)                 \
  X(ERR_DEV_TYPE_UNKNOWN)            \
  X(ERR_DEV_CREATE_SIZE)

#define LOG_BODY_ERR_DEV_NOT_FOUND(p, out, out_size) snprintf((out), (out_size), "device %u is not registered", (p)->dev_id)
#define LOG_BODY_ERR_DEV_ALREADY_EXIST(p, out, out_size) snprintf((out), (out_size), "device %u is already registered", (p)->dev_id)
#define LOG_BODY_ERR_DEV_FEATURE_UNAVAILABLE(p, out, out_size)                                                     \
  do {                                                                                                              \
    const char* __contract = sys_device_contract_name((p)->contract_id);                                            \
    const char* __feature = sys_device_feature_name((p)->contract_id, (p)->feature_id);                             \
    if (__contract == NULL) __contract = "UNKNOWN";                                                                 \
    if (__feature) {                                                                                                \
      snprintf((out), (out_size), "device %u: feature %s unavailable on contract %s", (p)->dev_id, __feature, __contract); \
    } else {                                                                                                        \
      snprintf((out), (out_size), "device %u: feature %u unavailable on contract %s", (p)->dev_id, (p)->feature_id, __contract); \
    }                                                                                                               \
  } while (0)
#define LOG_BODY_ERR_DEV_SUSPENDED(p, out, out_size) snprintf((out), (out_size), "device %u is suspended", (p)->dev_id)
#define LOG_BODY_ERR_DEV_NOT_INSTALLED(p, out, out_size) snprintf((out), (out_size), "device %u is registered but not installed", (p)->dev_id)
#define LOG_BODY_ERR_DEV_INSTALL_FAILED(p, out, out_size) snprintf((out), (out_size), "device %u failed to install", (p)->dev_id)
#define LOG_BODY_ERR_DEV_INSTALL_STEP_FAILED(p, out, out_size) snprintf((out), (out_size), "install step failed (line %u)", (unsigned)(p)->line)
#define LOG_BODY_ERR_DEV_ONBOARD(p, out, out_size) snprintf((out), (out_size), "device %u is onboard (baked onto the PCB) and can't be uninstalled by a user", (p)->dev_id)
#define LOG_BODY_ERR_DEV_PIN_ORDER(p, out, out_size) snprintf((out), (out_size), "device %u: pin %u is on device %u; a device's pins must be on a lower-ID device", (p)->dev_id, (p)->pin, (p)->pin_dev_id)
/* A rejected command, not a device fault: not attributed to the device by device_id_of(). */
#define LOG_BODY_ERR_DEV_OP_UNKNOWN(p, out, out_size) snprintf((out), (out_size), "device %u has no operation 0x%02X", (p)->dev_id, (p)->op)
#define LOG_BODY_ERR_DEV_OP_SIZE(p, out, out_size) snprintf((out), (out_size), "device %u operation 0x%02X: got %u argument bytes, expected %u", (p)->dev_id, (p)->op, (unsigned)(p)->got, (unsigned)(p)->need)
#define LOG_BODY_ERR_DEV_TYPE_UNKNOWN(p, out, out_size) snprintf((out), (out_size), "no device type 0x%02X is registered", (p)->type_id)
#define LOG_BODY_ERR_DEV_CREATE_SIZE(p, out, out_size) snprintf((out), (out_size), "create device type 0x%02X: got %u config bytes, expected %u", (p)->type_id, (unsigned)(p)->got, (unsigned)(p)->need)
#define LOG_BODY_ERR_DEV_FAULT_RESPONSE_FAILED(p, out, out_size) \
  snprintf((out), (out_size), "device %u fault response failed (level=%u, stage=%u, action=%u, cause_tag=%u)", \
           (p)->dev_id, (p)->level, (p)->stage, (p)->action_id, (unsigned)(p)->cause_tag)

/**
 * @brief Tags that attribute a failure to the device in `dev_id` (see SYS_ERROR_DEVICE_TAGS in sys_error_codes.h).
 * Rejected commands (ONBOARD, PIN_ORDER, OP_UNKNOWN, OP_SIZE, TYPE_UNKNOWN, CREATE_SIZE) and
 * ERR_DEV_FAULT_RESPONSE_FAILED are deliberately not here: they are not faults of the device.
 */
#define SYS_ERROR_DEV_DEVICE_TAGS(X) \
  X(ERR_DEV_INSTALL_FAILED)          \
  X(ERR_DEV_NOT_FOUND)               \
  X(ERR_DEV_ALREADY_EXIST)           \
  X(ERR_DEV_FEATURE_UNAVAILABLE)     \
  X(ERR_DEV_SUSPENDED)               \
  X(ERR_DEV_NOT_INSTALLED)
