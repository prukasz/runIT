#pragma once
/**
 * @file devices.h
 * @brief Every device's public header (its create struct and constants), for code that names several chips: the board layout.
 * A new device adds its header here and its component to the REQUIRES of this component's CMakeLists.txt.
 */
#include "device_ads7128.h"
#include "device_ap33772s.h"
#include "device_dac53202.h"
#include "device_drv8962.h"
#include "device_gpio_esp.h"
#include "device_ina3221.h"
#include "device_pca9685.h"
#include "device_power_mux.h"
#include "device_servo.h"
#include "device_tca6424a.h"
#include "device_tps55289.h"
