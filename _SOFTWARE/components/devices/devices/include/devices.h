#pragma once

//Add device owner code here
#include "devices_owners.h"
#include "sys_error.h"

//Include all devices headers here so they are globally available
#include "device_ads7128.h"
#include "device_ap33772s.h"
#include "device_dac53202.h"
#include "device_gpio_esp.h"
#include "device_ina3221.h"
#include "device_pca9685.h"
#include "device_tca6424a.h"
#include "device_tps55289.h"
#include "device_drv8962.h"
#include "device_servo.h"

/**
 * @brief Register every device class with sys_device, so the packet router can create them by type id.
 *
 * Boot step; runs before sys_interface_init() starts the RX receiver and before the boot action.
 */
SE_MUST_USE err_h devices_register_classes(void);
