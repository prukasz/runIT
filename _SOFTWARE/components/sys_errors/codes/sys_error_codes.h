#pragma once
#include <stdint.h>
#include "esp_err.h"

// Include all sub-modules
#include "base/sys_error_base.h"
#include "devices_owners.h"
#include "sys_error_dev.h"
#include "sys_error_io.h"
#include "sys_error_i2c.h"
#include "sys_error_power.h"
#include "sys_error_ble.h"
#include "sys_error_interface.h"
#include "sys_error_buffers.h"
#include "sys_error_actions.h"
#include "sys_error_vm.h"

// Combine all error maps into one global map.
// This allows us to auto-generate the enums and the individual payload structures.
#define SYS_ERROR_MAP(X) \
    SYS_ERROR_BASE_MAP(X) \
    SYS_ERROR_DEV_MAP(X) \
    SYS_ERROR_IO_MAP(X) \
    SYS_ERROR_I2C_MAP(X) \
    SYS_ERROR_POWER_MAP(X) \
    SYS_ERROR_BLE_MAP(X) \
    SYS_ERROR_INTERFACE_MAP(X) \
    SYS_ERROR_BUFFERS_MAP(X) \
    SYS_ERROR_ACTIONS_MAP(X) \
    SYS_ERROR_VM_MAP(X)

// Combine all owner maps
#define SYS_OWNER_MAP(X) \
    SYS_ERRORS_OWNER_MAP(X) \
    SYS_DEVICE_OWNER_MAP(X) \
    SYS_IO_OWNER_MAP(X) \
    SYS_I2C_OWNER_MAP(X) \
    SYS_POWER_OWNER_MAP(X) \
    SYS_BLE_OWNER_MAP(X) \
    SYS_INTERFACE_OWNER_MAP(X) \
    SYS_BUFFERS_OWNER_MAP(X) \
    SYS_ACTIONS_OWNER_MAP(X) \
    SYS_VM_OWNER_MAP(X) \
    PROVIDER_OWNER_MAP(X)

// Combine every module's opt-in payload-description map (X(tag) only - see
// sys_error_base.h's LOGGER_MAP comment for why the typed functions are
// generated separately, in sys_error.h, after the payload structs exist).
#define SYS_ERROR_LOGGER_MAP(X) \
    SYS_ERROR_BASE_LOGGER_MAP(X) \
    SYS_ERROR_DEV_LOGGER_MAP(X) \
    SYS_ERROR_IO_LOGGER_MAP(X) \
    SYS_ERROR_I2C_LOGGER_MAP(X) \
    SYS_ERROR_POWER_LOGGER_MAP(X) \
    SYS_ERROR_BLE_LOGGER_MAP(X) \
    SYS_ERROR_INTERFACE_LOGGER_MAP(X) \
    SYS_ERROR_BUFFERS_LOGGER_MAP(X) \
    SYS_ERROR_ACTIONS_LOGGER_MAP(X) \
    SYS_ERROR_VM_LOGGER_MAP(X)

#define X_OWNER_ENUM(tag, id, name) tag = id,
typedef enum {
    SYS_OWNER_MAP(X_OWNER_ENUM)
    OWNER_MAX_COUNT
} sys_owner_e;
#undef X_OWNER_ENUM
