#pragma once

/* The wiring of the runIT board, rev 1, used by runit_r1_devices.c. BOARDS.MD: not compiled. */

/* The runIT board, rev 1. */

/* Device IDs of the onboard chips. */
#define DEVICE_ID_ESP32S3_GPIO     0u
#define DEVICE_ID_TCA6424A         1u
#define DEVICE_ID_ADS7128          2u
#define DEVICE_ID_PCA9685          3u
#define DEVICE_ID_DAC53202         4u
#define DEVICE_ID_DRV8962_0        5u
#define DEVICE_ID_DRV8962_1        6u
#define DEVICE_ID_SUPPLY_DRV8962_0 7u
#define DEVICE_ID_SUPPLY_DRV8962_1 8u
#define DEVICE_ID_TPS55289_2       10u
#define DEVICE_ID_TPS55289_1       11u
#define DEVICE_ID_INA3221          12u
#define DEVICE_ID_AP33772S         13u

/* Power (described to sys_power_manager in runit_board_cfg.c) */
#define RUNIT_BOARD_TCA_VUSB_OK 3u  // eFuse power good, USB-C input (high = live)
#define RUNIT_BOARD_TCA_VEXT_OK 4u  // eFuse power good, external input (high = live)

#define RUNIT_BOARD_POWER_LIMIT_MV          20000  // hardware trip: highest input voltage
#define RUNIT_BOARD_POWER_LIMIT_MA          5500   // hardware trip: highest input current
#define RUNIT_BOARD_POWER_RESERVE_MW        3667   // 3.3 V buck for the ESP32: 3.3 V x 1 A at ~90 %
#define RUNIT_BOARD_POWER_UNKNOWN_SOURCE_MA 1000   // assumed while the source's current limit is unknown (USB too)
#define RUNIT_BOARD_VREG_EFFICIENCY_PCT     90     // TPS55289 rails

/* A devkit (CONFIG_RUNIT_BOARD_DEVKIT): USB only. */
#define RUNIT_DEVKIT_POWER_LIMIT_MV   5000  // USB VBUS
#define RUNIT_DEVKIT_POWER_LIMIT_MA   500   // a USB 2.0 port
#define RUNIT_DEVKIT_POWER_RESERVE_MW 1000  // the ESP32-S3 module with its radio, through the devkit LDO
/* INA3221 channels, measured on the PCB 2026-09-24: rail A on 2, the board input on 1, rail B on 0. */
#define RUNIT_BOARD_INA_CH_RAIL_B  0u
#define RUNIT_BOARD_INA_CH_INPUT   1u
#define RUNIT_BOARD_INA_CH_RAIL_A  2u

#define RUNIT_BOARD_TCA_ADDR        0x23u  // ADDR high; INT -> ESP 9, RST -> ESP 8
#define RUNIT_BOARD_TCA_INT_ESP_PIN 9u
#define RUNIT_BOARD_TCA_RST_ESP_PIN 8u
#define RUNIT_BOARD_TCA_LED_STATUS  23u  // status LED on the expander (high = on), lit once the boot has created the expander
#define RUNIT_BOARD_TCA_LED_SPARE   22u   // the other LED: an output too, else its floating input toggles at ~25 kHz and floods the INT line

/* TCA6424A pins (HARDWARE.md section 3) */
#define RUNIT_BOARD_TCA_PCA_OE      0u
#define RUNIT_BOARD_TCA_TPS_A_INT   1u
#define RUNIT_BOARD_TCA_TPS_B_INT   2u
#define RUNIT_BOARD_TCA_INA_WARN    5u
#define RUNIT_BOARD_TCA_INA_CRIT    6u
#define RUNIT_BOARD_TCA_DRV2_NSLEEP 8u
#define RUNIT_BOARD_TCA_DRV1_NSLEEP 9u
#define RUNIT_BOARD_TCA_DRV2_NFAULT 10u
#define RUNIT_BOARD_TCA_DRV1_NFAULT 11u
/* DRV8962 #2's LM73100 pair is wired the other way round from the schematic (measured on the PCB 2026-10-11: with TCA 12
   selected the bridge drew nothing from rail A and gave 50 % of 5.06 V = the board input; with TCA 13 selected the bridge
   saw rail A drop and raised nFAULT (UVLO)). #1 is as documented. */
#define RUNIT_BOARD_TCA_DRV2_VM_VSUP   12u  // LM73100 (high = on)
#define RUNIT_BOARD_TCA_DRV2_VM_RAIL_A 13u
#define RUNIT_BOARD_TCA_DRV1_VM_VSUP   14u
#define RUNIT_BOARD_TCA_DRV1_VM_RAIL_B 15u
#define RUNIT_BOARD_TCA_TPS_B_EN       16u
#define RUNIT_BOARD_TCA_TPS_A_EN       17u
#define RUNIT_BOARD_TCA_AP_INT         21u
#define RUNIT_BOARD_ADS_ALERT_ESP_PIN  42u
#define RUNIT_BOARD_ADS_FULL_SCALE_MV  20000u  // the front end scales the inputs to 20 V
#define RUNIT_BOARD_DRV8962_LIMIT_MA   2000u   // the chip's chopping limit (every channel) until the user sets one
#define RUNIT_BOARD_DRV8962_PCA_PWM_HZ 50u     // the PCA9685 has one frequency for CH 0..15: the servos on CH 0..7 want 50 Hz

/* All three shunts of the INA3221 are wired reversed (inverted_mask = 0x07). */
#define RUNIT_BOARD_INA_SHUNT_MOHM 10  // the PCB's three shunts (mOhm)
