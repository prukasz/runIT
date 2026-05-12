#include "manager_i2c_errors.h"
#include "rik_error_codes.h"
#include "ina3221_wrapper_errors.h"

#define GLOBAL_OWNER_MAP(X) \
    X(OWNER_SYSTEM_CORE, 0x0001, "SYSTEM_CORE") \
    I2C_OWNER_MAP(X) \
    RIK_OWNER_MAP(X) \
    INA3221_OWNER_MAP(X) \

    /* BLE_OWNER_MAP(X) */

// Generate the Enum...
#define X_ENUM(name, value, str_name) name = value,
typedef enum { GLOBAL_OWNER_MAP(X_ENUM) } status_owner_e;
#undef X_ENUM

#define GLOBAL_ERROR_MAP(X) \
    X(ERR_OK, 0x0000, "OK") \
    X(ERR_UNKNOWN, 0x00FF, "UNKNOWN_ERROR") \
    I2C_ERROR_MAP(X) \
    RIK_ERROR_MAP(X) \
    INA3221_ERROR_MAP(X) \
    
#define X_ENUM(name, value, str_name) name = value,
typedef enum { GLOBAL_ERROR_MAP(X_ENUM) } status_code_e;
#undef X_ENUM

