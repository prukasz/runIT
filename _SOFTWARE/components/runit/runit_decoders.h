#pragma once

#include "sys_error.h"

/**
 * @brief Register every inbound packet class with sys_interface.
 *
 * Boot step; runs before sys_interface_init() starts the RX receiver.
 */
SE_MUST_USE err_h runit_register_decoders(void);
