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
// INA3221 channels (0-based), measured on the PCB 2026-09-24: rail A (TPS55289 0x74,
// DEVICE_ID_TPS55289_0) on 2, the board input on 1, rail B (0x75, _1) on 0.
// All three shunts are wired reversed by design (INA3221 inverted_mask = 0x07).
#define RUNIT_BOARD_INA_CH_RAIL_B 0
#define RUNIT_BOARD_INA_CH_INPUT 1
#define RUNIT_BOARD_INA_CH_RAIL_A 2
/*BOARD POWER*/

/*TCA6424A pins (legacy firmware header, matches the PCB)*/
#define RUNIT_BOARD_TCA_VUSB_OK 3            // eFuse power good, USB-C input
#define RUNIT_BOARD_TCA_VEXT_OK 4            // eFuse power good, external input
#define RUNIT_BOARD_TCA_DRV_OCPM 7           // DRV8962 OCPM (fault recovery mode), both chips
#define RUNIT_BOARD_TCA_DRV2_VM_RAIL_A 12    // LM73100: DRV8962 #2 VM <- rail A (TPS55289 0x74)
#define RUNIT_BOARD_TCA_DRV2_VM_VSUP 13      // LM73100: DRV8962 #2 VM <- board input
#define RUNIT_BOARD_TCA_DRV1_VM_VSUP 14      // LM73100: DRV8962 #1 VM <- board input
#define RUNIT_BOARD_TCA_DRV1_VM_RAIL_B 15    // LM73100: DRV8962 #1 VM <- rail B (TPS55289 0x75)
/*TCA6424A pins*/

#define DEVICE_ID_GPIO_ESP 0  //@STATIC_DEVICE
#define DEVICE_ID_TCA6424A 1  //@STATIC_DEVICE
#define DEVICE_ID_ADS7128 2   //@STATIC_DEVICE
#define DEVICE_ID_PCA9685 3   //@STATIC_DEVICE
#define DEVICE_ID_DAC53202 4  //@STATIC_DEVICE
#define DEVICE_ID_DRV8962_0 5 //@STATIC_DEVICE
#define DEVICE_ID_DRV8962_1 6 //@STATIC_DEVICE

#define DEVICE_ID_TPS55289_0 10  //@STATIC_DEVICE
#define DEVICE_ID_TPS55289_1 11  //@STATIC_DEVICE
#define DEVICE_ID_INA3221 12     //@STATIC_DEVICE
#define DEVICE_ID_AP33772S 13    //@STATIC_DEVICE

#define SYS_BLE_SVC_RUNIT 0xFFE0         //@STATIC_SERVICE
#define SYS_BLE_CHR_RUNIT_TX 0xFFE1      //@STATIC_CHARACTERISTIC
#define SYS_BLE_CHR_RUNIT_RX 0xFFE2      //@STATIC_CHARACTERISTIC
#define SYS_BLE_CHR_RUNIT_LOGS 0xFFE3    //@STATIC_CHARACTERISTIC
#define SYS_BLE_CHT_RUNIT_STATUS 0xFFE4  //@STATIC_CHARACTERISTIC

/* Transport providers this board registers with sys_data_connector. Published
   to the app (enums.json) for the data connector settings packets; the values
   come from Kconfig ("Data Connector Registry"). */
#include <sdkconfig.h>
//#ref-enum @alias Data Provider
typedef enum runit_data_provider_e {
  RUNIT_DATA_PROVIDER_BLE = CONFIG_SYS_DATA_PROVIDER_ID_BLE, //@alias Bluetooth LE @description The endpoint is a characteristic UUID.
  RUNIT_DATA_PROVIDER_UART = CONFIG_SYS_DATA_PROVIDER_ID_UART, //@alias UART console @description Frames are "#R:<hex>" lines on the console UART, next to the text log. The only endpoint is 0.
} runit_data_provider_e;
