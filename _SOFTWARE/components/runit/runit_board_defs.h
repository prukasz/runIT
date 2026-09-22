#pragma once
/*BOARD I2C CONFIG*/
#define SYS_I2C_BUS_INTERNAL 0
#define SYS_I2C_BUS_USER 1
#define SYS_I2C_PULLUP_ENABLE true

#define SYS_PIN_I2C_0_SDA 15  // internal
#define SYS_PIN_I2C_0_SCL 16  // internal
#define SYS_PIN_I2C_1_SDA 40  // external
#define SYS_PIN_I2C_1_SCL 41  // external
/*BOARD I2C CONFIG*/

/*BOARD POWER (rev 1) - described to sys_power in runit_board_cfg.c*/
#define RUNIT_BOARD_POWER_LIMIT_MV 20000          // hardware trip: highest input voltage
#define RUNIT_BOARD_POWER_LIMIT_MA 5500           // hardware trip: highest input current
#define RUNIT_BOARD_POWER_RESERVE_MW 3667         // 3.3 V buck for the ESP32: 3.3 V x 1 A at ~90 %
#define RUNIT_BOARD_POWER_UNKNOWN_SOURCE_MA 1000  // assumed while the source's current limit is unknown
#define RUNIT_BOARD_VREG_EFFICIENCY_PCT 90        // TPS55289 rails
// INA3221 channels. TODO: confirm against the schematic (HARDWARE.md lists the order as unknown).
#define RUNIT_BOARD_INA_CH_RAIL_A 0
#define RUNIT_BOARD_INA_CH_RAIL_B 1
#define RUNIT_BOARD_INA_CH_INPUT 2
/*BOARD POWER*/

#define DEVICE_ID_GPIO_ESP 0  //@STATIC_DEVICE
#define DEVICE_ID_TCA6424A 1  //@STATIC_DEVICE
#define DEVICE_ID_ADS7128 2   //@STATIC_DEVICE
#define DEVICE_ID_PCA9685 3   //@STATIC_DEVICE
#define DEVICE_ID_DAC53202 4  //@STATIC_DEVICE

#define DEVICE_ID_TPS55289_0 10  //@STATIC_DEVICE
#define DEVICE_ID_TPS55289_1 11  //@STATIC_DEVICE
#define DEVICE_ID_INA3221 12     //@STATIC_DEVICE
#define DEVICE_ID_AP33772S 13    //@STATIC_DEVICE

#define SYS_BLE_SVC_RUNIT 0xFFE0         //@STATIC_SERVICE
#define SYS_BLE_CHR_RUNIT_TX 0xFFE1      //@STATIC_CHARACTERISTIC
#define SYS_BLE_CHR_RUNIT_RX 0xFFE2      //@STATIC_CHARACTERISTIC
#define SYS_BLE_CHR_RUNIT_LOGS 0xFFE3    //@STATIC_CHARACTERISTIC
#define SYS_BLE_CHT_RUNIT_STATUS 0xFFE4  //@STATIC_CHARACTERISTIC
