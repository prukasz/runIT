#pragma once
#include "sys_data_connector.h"

/**
 * @file sys_data_connector_ble.h
 * @brief BLE transport provider adapter for sys_data_connector.
 */

#define SYS_DATA_BLE_ARG(char_uuid) ((void*)(uintptr_t)(uint16_t)(char_uuid))
#define SYS_DATA_BLE_ARG_CHAR(arg)  ((uint16_t)(uintptr_t)(arg))

/**
 * @brief Registers the BLE transport provider driver with the data connector bus.
 *
 * @return err_h NULL on success.
 */
SE_MUST_USE err_h sys_data_connector_register_ble_provider(void);

