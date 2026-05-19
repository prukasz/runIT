#pragma once
#include "ina3221_wrapper_errors.h"
#include "tps55289_wrapper_errors.h"

#define MANAGER_PWR_OWNER_MAP(X) \
    X( OWNER_MANAGER_PWR, 0xB000, "OWNER_MANAGER_PWR")\
    INA3221_OWNER_MAP(X) \
    TPS55289_OWNER_MAP(X) 

#define MANAGER_PWR_ERROR_MAP(X)\
    X(ERR_MANAGER_PWR_BASE,        0xB000, "ERR_MANAGER_PWR_BASE")\
    INA3221_ERROR_MAP(X)\
    TPS55289_ERROR_MAP(X) 
    