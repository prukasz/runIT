#pragma once
#include "provider_current_monitor_errors.h"
#include "provider_voltage_regulator_errors.h"

#define MANAGER_PWR_OWNER_MAP(X) \
    X( OWNER_MANAGER_PWR, 0xB000, "OWNER_MANAGER_PWR")\
    PROVIDER_CURRENT_MONITOR_OWNER_MAP(X) \
    PROVIDER_VREG_OWNER_MAP(X) 

#define MANAGER_PWR_ERROR_MAP(X)\
    X(ERR_MANAGER_PWR_BASE,        0xB000, "ERR_MANAGER_PWR_BASE")\
    PROVIDER_CURRENT_MONITOR_ERROR_MAP(X)\
    PROVIDER_VREG_ERROR_MAP(X) 
    