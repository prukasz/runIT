#pragma once

#include "sys_error.h"

// -----------------------------------------------------------------------------
// Domain Error Hooks (Weak Declarations)
//
// These weak hooks allow domain components to register custom error/fault
// handlers without creating tight coupling or circular dependencies.
// If a component does not provide an implementation, the linker sets the symbol
// to NULL and the dispatcher safely bypasses it.
// -----------------------------------------------------------------------------

extern err_h sys_device_report_error_with_level(uint8_t device_id, sys_device_err_level_e level, err_h error) __attribute__((weak));
extern bool sys_device_is_ignored(uint8_t device_id) __attribute__((weak));

extern err_h sys_i2c_report_fault(err_h node, err_h chain) __attribute__((weak));
extern err_h sys_io_report_fault(err_h node, err_h chain) __attribute__((weak));
extern err_h sys_power_report_fault(err_h node, err_h chain) __attribute__((weak));
extern err_h sys_ble_report_fault(err_h node, err_h chain) __attribute__((weak));
extern err_h sys_interface_report_fault(err_h node, err_h chain) __attribute__((weak));
extern err_h sys_buffers_report_fault(err_h node, err_h chain) __attribute__((weak));
extern err_h sys_errors_report_fault(err_h node, err_h chain) __attribute__((weak));
extern err_h sys_vm_report_fault(err_h node, err_h chain) __attribute__((weak));
extern err_h sys_actions_report_fault(err_h node, err_h chain) __attribute__((weak));


// System response never looks up a device ID. All hooks borrow node/chain and
// return an owned response failure, or NULL. They must not retain the chain.
extern err_h sys_system_report_fault(err_h node, err_h chain) __attribute__((weak));
