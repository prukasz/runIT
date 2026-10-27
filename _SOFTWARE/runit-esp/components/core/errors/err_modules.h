#pragma once

/**
 * @file err_modules.h
 * @brief Error module IDs (high 16 bits of every tag) - the one place they are
 * assigned, so they stay unique across the firmware. In order; a new module
 * takes the next number. Never renumber or reuse one. Included by errors.h.
 */

#define ERR_MOD_CORE               0u  // errors itself (errors.h)
#define ERR_MOD_SYS_I2C            1u  // components/sys/sys_i2c
#define ERR_MOD_ROUTER             2u  // components/core/router
#define ERR_MOD_SYS_UART           3u  // components/sys/sys_uart
#define ERR_MOD_SYS_BLE            4u  // components/sys/sys_ble
#define ERR_MOD_SYS_DATA_CONNECTOR 5u  // components/sys/sys_data_connector
#define ERR_MOD_SYS_DEVICE         6u  // components/sys/sys_device (also the errors shared by all devices)
#define ERR_MOD_SYS_IO             7u  // components/sys/sys_io (the IO contract: errors of devices with pins)
#define ERR_MOD_SYS_ACTIONS        8u  // components/sys/sys_actions
#define ERR_MOD_SYS_EVENT          9u  // components/sys/sys_event
#define ERR_MOD_SYS_POWER          10u // components/sys/sys_power (the power contracts: monitor, vreg)
#define ERR_MOD_CMD                11u // components/core/cmd (every command handler: payload size, answer size, argument)
// 12 is unused: never assign it.
#define ERR_MOD_SYS_LOG            13u // components/sys/sys_log (log output options)
#define ERR_MOD_SYS_POWER_MANAGER  14u // components/sys/sys_power_manager (budget, source, battery, responses)
#define ERR_MOD_VM                 15u // components/vm/vm_core (the program runtime: store, objects, accessors, blocks, loader, exec)
#define ERR_MOD_SYS_PROJECT        16u // components/sys/sys_project (the stored project: stream in, staged records, run)
