#pragma once

/* The runIT board, rev 1. */

/* Device IDs of the onboard chips, created at boot by runit_board_devices_init (runit_board_cfg.c). Published for the app
 * (board/generate-board.py): the name each one is shown by, and its type from the create call. */
//#ref-defines board_device @alias Board devices @prefix DEVICE_ID_
#define DEVICE_ID_GPIO_ESP 0u  //@alias ESP32 GPIO @description the ESP32-S3's own pins
#define DEVICE_ID_TCA6424A 1u  //@alias GPIO expander @description TCA6424A, 24 pins (LEDs, interrupts, enables)
#define DEVICE_ID_ADS7128 2u  //@alias ADC @description ADS7128, 8 inputs scaled to 20 V
#define DEVICE_ID_PCA9685 3u  //@alias PWM expander @description PCA9685 (servo headers CH 0..7, DRV8962 #2 on CH 8..15)
#define DEVICE_ID_DAC53202 4u  //@alias DAC @description DAC53202 (OUT0 / OUT1 = VREF of DRV8962 #1 / #2)
#define DEVICE_ID_DRV8962_0 5u  //@alias Motor driver 1 @description DRV8962 #1 (IN / EN / IPROPI on ESP)
#define DEVICE_ID_DRV8962_1 6u  //@alias Motor driver 2 @description DRV8962 #2 (IN / EN on PCA9685 CH 8..15)
#define DEVICE_ID_SUPPLY_DRV8962_0 7u  //@alias Motor 1 supply @description Power mux of driver 1 (A = board input, B = rail B)
#define DEVICE_ID_SUPPLY_DRV8962_1 8u  //@alias Motor 2 supply @description Power mux of driver 2 (A = board input, B = rail A)
#define DEVICE_ID_TPS55289_2 10u  //@alias Rail A @description TPS55289 buck-boost regulator at 0x74
#define DEVICE_ID_TPS55289_1 11u  //@alias Rail B @description TPS55289 buck-boost regulator at 0x75
#define DEVICE_ID_INA3221 12u  //@alias Power monitor @description INA3221 (the board input and both rails)
#define DEVICE_ID_AP33772S 13u  //@alias USB-C PD @description AP33772S USB-C power delivery sink

#define RUNIT_BOARD_TCA_ADDR 0x23u  // ADDR high; INT -> ESP 9, RST -> ESP 8
#define RUNIT_BOARD_TCA_INT_ESP_PIN 9u
#define RUNIT_BOARD_TCA_RST_ESP_PIN 8u
#define RUNIT_BOARD_TCA_LED_STATUS 23u  // status LED on the expander (high = on), lit once the boot has created the expander
#define RUNIT_BOARD_TCA_LED_SPARE 22u   // the other LED: an output too, else its floating input toggles at ~25 kHz and floods the INT line

/* Power (described to sys_power_manager in runit_board_cfg.c) */
/* TCA6424A pins (HARDWARE.md section 3) */
#define RUNIT_BOARD_TCA_PCA_OE 0u
#define RUNIT_BOARD_TCA_VUSB_OK 3u  // eFuse power good, USB-C input (high = live)
#define RUNIT_BOARD_TCA_VEXT_OK 4u  // eFuse power good, external input (high = live)
#define RUNIT_BOARD_TCA_TPS_A_INT 1u
#define RUNIT_BOARD_TCA_TPS_B_INT 2u
#define RUNIT_BOARD_TCA_INA_WARN 5u
#define RUNIT_BOARD_TCA_INA_CRIT 6u
#define RUNIT_BOARD_TCA_DRV2_NSLEEP 8u
#define RUNIT_BOARD_TCA_DRV1_NSLEEP 9u
#define RUNIT_BOARD_TCA_DRV2_NFAULT 10u
#define RUNIT_BOARD_TCA_DRV1_NFAULT 11u
/* DRV8962 #2's LM73100 pair is wired the other way round from the schematic (measured on the PCB 2026-10-11: with TCA 12
   selected the bridge drew nothing from rail A and gave 50 % of 5.06 V = the board input; with TCA 13 selected the bridge
   saw rail A drop and raised nFAULT (UVLO)). #1 is as documented. */
#define RUNIT_BOARD_TCA_DRV2_VM_VSUP 12u  // LM73100 (high = on)
#define RUNIT_BOARD_TCA_DRV2_VM_RAIL_A 13u
#define RUNIT_BOARD_TCA_DRV1_VM_VSUP 14u
#define RUNIT_BOARD_TCA_DRV1_VM_RAIL_B 15u
#define RUNIT_BOARD_TCA_TPS_B_EN 16u
#define RUNIT_BOARD_TCA_TPS_A_EN 17u
#define RUNIT_BOARD_TCA_AP_INT 21u
#define RUNIT_BOARD_ADS_ALERT_ESP_PIN 42u
#define RUNIT_BOARD_ADS_FULL_SCALE_MV 20000u  // the front end scales the inputs to 20 V
#define RUNIT_BOARD_DRV8962_LIMIT_MA 2000u    // chopping limit of every channel until the user sets one
#define RUNIT_BOARD_DRV8962_PCA_PWM_HZ 1000u  // the PCA9685 has one frequency for CH 0..15 (a PCA9685 tops at 1526 Hz)

#define RUNIT_BOARD_POWER_LIMIT_MV 20000          // hardware trip: highest input voltage
#define RUNIT_BOARD_POWER_LIMIT_MA 5500           // hardware trip: highest input current
#define RUNIT_BOARD_POWER_RESERVE_MW 3667         // 3.3 V buck for the ESP32: 3.3 V x 1 A at ~90 %
#define RUNIT_BOARD_POWER_UNKNOWN_SOURCE_MA 1000  // assumed while the source's current limit is unknown (USB too)
#define RUNIT_BOARD_VREG_EFFICIENCY_PCT 90        // TPS55289 rails

/* A devkit (CONFIG_RUNIT_BOARD_DEVKIT): USB only. */
#define RUNIT_DEVKIT_POWER_LIMIT_MV 5000  // USB VBUS
#define RUNIT_DEVKIT_POWER_LIMIT_MA 500   // a USB 2.0 port
#define RUNIT_DEVKIT_POWER_RESERVE_MW 1000  // the ESP32-S3 module with its radio, through the devkit LDO
/* INA3221 channels, measured on the PCB 2026-09-24: rail A on 2, the board input on 1, rail B on 0. All three shunts
 * are wired reversed (INA3221 inverted_mask = 0x07). */
#define RUNIT_BOARD_INA_SHUNT_MOHM 10  // the PCB's three shunts (mOhm)
#define RUNIT_BOARD_INA_CH_RAIL_B 0u
#define RUNIT_BOARD_INA_CH_INPUT 1u
#define RUNIT_BOARD_INA_CH_RAIL_A 2u
