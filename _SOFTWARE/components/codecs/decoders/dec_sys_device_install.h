#pragma once
/**
 * @file dec_sys_device_install.h
 * @brief Aggregates device installation packets for System Contracts (0x01).
 *
 * Every device owns one header in `decoders/device/`. That header contains
 * its wire packet, decoder, and field annotations used by the generated API
 * catalog. This file only supplies common pin conversion and folds their
 * packet lists into the existing System Contracts dispatcher.
 */

#include "device/dec_device_common.h"

#include "device/dec_device_gpio_esp.h"
#include "device/dec_device_pca9685.h"
#include "device/dec_device_tca6424a.h"
#include "device/dec_device_tps55289.h"
#include "device/dec_device_ina3221.h"
#include "device/dec_device_ap33772s.h"
#include "device/dec_device_dac53202.h"
#include "device/dec_device_ads7128.h"

#define SYS_CONTRACTS_INSTALL_PACKET_LIST(X)          \
  SYS_CONTRACTS_DEVICE_GPIO_ESP_PACKET_LIST(X)         \
  SYS_CONTRACTS_DEVICE_PCA9685_PACKET_LIST(X)          \
  SYS_CONTRACTS_DEVICE_TCA6424A_PACKET_LIST(X)         \
  SYS_CONTRACTS_DEVICE_TPS55289_PACKET_LIST(X)         \
  SYS_CONTRACTS_DEVICE_INA3221_PACKET_LIST(X)          \
  SYS_CONTRACTS_DEVICE_AP33772S_PACKET_LIST(X)         \
  SYS_CONTRACTS_DEVICE_DAC53202_PACKET_LIST(X)         \
  SYS_CONTRACTS_DEVICE_ADS7128_PACKET_LIST(X)
