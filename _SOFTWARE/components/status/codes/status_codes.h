#pragma once

#include <stddef.h>
#include <stdint.h>

// Include your sub-maps
#include "manager_i2c_errors.h"
#include "rik_error_codes.h"
#include "manager_io_errors.h"
#include "manager_power_errors.h"

// 1. Define the Global Maps (No trailing backslash on the last line!)
#define GLOBAL_OWNER_MAP(X) \
    X(OWNER_SYSTEM_CORE, 0x0001, "SYSTEM_CORE") \
    X(OWNER_ESP, 0x0002, "ESP_IDF") \
    I2C_OWNER_MAP(X) \
    RIK_OWNER_MAP(X) \
    MANAGER_PWR_OWNER_MAP(X) \
    MANAGER_IO_OWNER_MAP(X)

#define GLOBAL_ERROR_MAP(X) \
    X(ERR_OK, 0x0000, "OK") \
    X(ERR_UNKNOWN, 0x00FF, "UNKNOWN_ERROR") \
    X(ERR_ESP, 0x0100, "ESP_ERROR") \
    I2C_ERROR_MAP(X) \
    RIK_ERROR_MAP(X) \
    MANAGER_PWR_ERROR_MAP(X) \
    MANAGER_IO_ERROR_MAP(X)

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