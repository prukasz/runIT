#pragma once 
#include "provider_gpio_expander_errors.h"

#define MANAGER_IO_OWNER_MAP(X) \
    X( OWNER_MANAGER_IO, 0xD000, "OWNER_MANAGER_IO")\
    PROVIDER_GPIO_EXPANDER_OWNER_MAP(X)

#define MANAGER_IO_ERROR_MAP(X)\
    X(ERR_MANAGER_IO_BASE,        0xD000, "ERR_MANAGER_IO_BASE")\
    X(ERR_MANAGER_IO_NO_FREE_PORT, 0xD001, "No free IO port available")\
    X(ERR_MANAGER_IO_INVALID_PORT, 0xD002, "Invalid IO port ID")\
    X(ERR_MANAGER_IO_FUNC_NULL,    0xD003, "IO port function is NULL")\
    X(ERR_MANAGER_IO_PIN_PROTECTED,0xD004, "IO pin operation blocked by protection lock")\
    PROVIDER_GPIO_EXPANDER_ERROR_MAP(X)
