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
// 3 is unused: never assign it.
#define ERR_MOD_SYS_BLE            4u  // components/sys/sys_ble
#define ERR_MOD_SYS_STREAM         5u  // components/sys/sys_stream
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
#define ERR_MOD_SYS_DATA_PROVIDER  17u // components/sys/sys_data_provider (the ways bytes go out and come in)
#define ERR_MOD_SYS_SESSION        18u // components/sys/sys_session (the frames, the peers, the dead-man)
#define ERR_MOD_SYS_WATCH          19u // components/sys/sys_watch (reads run by a trigger, their entries)
#define ERR_MOD_MQTT   20u
#define ERR_MOD_SYS_REMOTE         21u // components/sys/sys_remote (devices of other boards: calls, subscriptions to their events and errors)
#define ERR_MOD_SYS_OTA            23u // components/sys/sys_ota (the firmware update)
#define ERR_MOD_SYS_NVS            22u // components/sys/sys_nvs (NVS partitions, JSON files in chunks)
#define ERR_MOD_SYS_UART           24u // components/sys/sys_uart (UART ports made at run time)
#define ERR_MOD_SYS_FLASHER        25u // components/sys/sys_flasher (the board on the link programmed through its ROM bootloader)
#define ERR_MOD_SYS_DISPLAY        26u // components/sys/sys_display (the display contract: GEOMETRY, BACKLIGHT, FLUSH, ORIENTATION)
#define ERR_MOD_SYS_TOUCH          27u // components/sys/sys_touch (the touch contract: POINT, CALIBRATION)
#define ERR_MOD_SYS_LVGL           28u // components/sys/sys_lvgl (the LVGL adapter: install, rotation, start, stop)
