#pragma once

/**
 * @file err_modules.h
 * @brief Error module IDs (high 16 bits of every tag) - the one place they are
 * assigned, so they stay unique across the firmware. In order; a new module
 * takes the next number. Never renumber or reuse one. Included by errors.h.
 *
 * A module that declares its own number (MODULE_ID_DECLARE, module_id.h) is not listed here: its error module is
 * that number.
 */

#define ERR_MOD_CORE               0u  // errors itself (errors.h)
// 3 is unused: never assign it.
#define ERR_MOD_SYS_BLE            4u  // components/sys/sys_ble
#define ERR_MOD_SYS_STREAM         5u  // components/sys/sys_stream
#define ERR_MOD_SYS_ACTIONS        8u  // components/sys/sys_actions
// 12 is unused: never assign it.
#define ERR_MOD_SYS_LOG            13u // components/sys/sys_log (log output options)
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
