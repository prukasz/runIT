#pragma once

#include <stddef.h>
#include <stdint.h>

// Include your sub-maps
#include "manager_i2c_errors.h"
#include "rik_error_codes.h"
#include "manager_io_errors.h"
#include "manager_power_errors.h"
#include "providers_errors.h"
// VM error/owner map
#include "vm/vm_errors.h"

// 1. Define the Global Maps (No trailing backslash on the last line!)
#define GLOBAL_OWNER_MAP(X) \
    I2C_OWNER_MAP(X) \
    RIK_OWNER_MAP(X) \
    MANAGER_PWR_OWNER_MAP(X) \
    PROVIDER_OWNER_MAP(X)\
    IO_OWNER_MAP(X) \
    VM_OWNER_MAP(X)

#define GLOBAL_ERROR_MAP(X) \
    X(ERR_OK, 0x0000, "OK") \
    X(ERR_UNKNOWN, 0xFFFF, "UNKNOWN_ERROR") \
    X(ERR_ESP, 0x0100, "ESP_ERROR") \
    X(ERR_MISSING_HANDLE, 0xf101, "No handle") \
    X(ERR_INVALID_ARG, 0xf102, "Invalid argument") \
    X(ERR_INVALID_PARAM, 0xf103, "Invalid parameter value") \
    X(ERR_NULL_POINTER, 0xf104, "Null pointer") \
    I2C_ERROR_MAP(X) \
    RIK_ERROR_MAP(X) \
    PWR_ERROR_MAP(X) \
    IO_ERROR_MAP(X) \
    VM_ERROR_MAP(X)

// 2. Generate the Enums
#define X_ENUM(name, value, str_name) name = value,

typedef enum { 
    GLOBAL_OWNER_MAP(X_ENUM) 
} status_owner_e;

typedef enum { 
    GLOBAL_ERROR_MAP(X_ENUM) 
} status_code_e;

#undef X_ENUM

// 3. Declare the functions
const char *status_owner_to_name(uint32_t owner);
const char *status_error_to_name(uint32_t error_code);