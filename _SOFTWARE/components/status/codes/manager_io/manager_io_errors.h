#pragma once 
#include "tca6424a_wrapper_errors.h"

#define MANAGER_IO_OWNER_MAP(X) \
    X( OWNER_MANAGER_IO, 0xD000, "OWNER_MANAGER_IO")\
    TCA6424A_OWNER_MAP(X)

#define MANAGER_IO_ERROR_MAP(X)\
    X(ERR_MANAGER_IO_BASE,        0xD000, "ERR_MANAGER_IO_BASE")\
    TCA6424A_ERROR_MAP(X)
