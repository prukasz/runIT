#pragma once

#include "sys_error.h"

/**
 * @brief Register every inbound packet class with sys_interface.
 *
 * Boot step; runs before sys_interface_init() starts the RX receiver.
 */
SE_MUST_USE err_h runit_register_decoders(void);

/**
 * @brief Replay the stored code (sys_project) and start the VM when autostart is set.
 *
 * Runtime boot step, after vm_exec_start(). Never aborts the boot: a replay
 * problem is reported and the board runs with what applied.
 */
SE_MUST_USE err_h runit_project_boot(void);
