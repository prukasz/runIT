#pragma once
#include <stdint.h>

// Include your sub-maps
#include "sys_status_codes.h"
#include "devices_owners.h"

// 1. Define the Global Maps (No trailing backslash on the last line!)
#define GLOBAL_OWNER_MAP(X) \
    SYS_DEVICE_OWNER_MAP(X) \
    SYS_I2C_OWNER_MAP(X) \
    SYS_IO_OWNER_MAP(X) \
    SYS_POWER_OWNER_MAP(X) \
    SYS_BLE_OWNER_MAP(X) \
    SYS_INTERFACE_OWNER_MAP(X) \
    PROVIDER_OWNER_MAP(X)
     
#define GLOBAL_ERROR_MAP(X) \
    X(ERR_OK, 0x0000, "OK") \
    X(ERR_UNKNOWN, 0xFFFF, "UNKNOWN_ERROR") \
    X(ERR_ESP, 0x0100, "ESP_ERROR") \
    X(ERR_MISSING_HANDLE, 0xf101, "No handle") \
    X(ERR_INVALID_VALUE, 0xf102, "Invalid value") \
    X(ERR_NULL_POINTER, 0xf104, "Null pointer") \
    X(ERR_NOT_FOUND, 0xf105, "Not found") \
    X(ERR_NOT_SUPPORTED, 0xf106, "Not supported") \
    X(ERR_UNAVAILABLE, 0xf107, "No memory") \
    X(ERR_NO_MEM, 0xf108, "No memory") \
    X(ERR_INVALID_ARG, 0xf109, "Invalid argument") \
    X(ERR_INVALID_STATE, 0xf10b, "Invalid state") \
    X(ERR_HARDWARE_FAULT, 0xf10a, "Hardware fault") \
    X(ERR_INVALID_SIZE, 0xf10c, "Invalid size") \
    SYS_DEVICE_ERROR_MAP(X) \
    SYS_I2C_ERROR_MAP(X) \
    SYS_IO_ERROR_MAP(X) \
    SYS_POWER_ERROR_MAP(X) \
    SYS_BLE_ERROR_MAP(X) \
    SYS_INTERFACE_ERROR_MAP(X)
// 2. Generate the Enums
#define X_ENUM_OWNER(name, value, str_name) name = value,
#define X_ENUM_ERROR(name, value, str_name) name = value,

typedef enum { 
    GLOBAL_OWNER_MAP(X_ENUM_OWNER) 
} status_owner_e;

typedef enum { 
    GLOBAL_ERROR_MAP(X_ENUM_ERROR) 
} status_code_e;

#undef X_ENUM_OWNER
#undef X_ENUM_ERROR

// 3. Declare the functions
const char *status_owner_to_name(uint32_t owner);
const char *status_error_to_name(uint32_t error_code);