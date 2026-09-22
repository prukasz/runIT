#pragma once

#include "dec_device_common.h"
#include "../../../devices/device_ads7128/include/device_ads7128.h"

//@id device_ads_7128
//@version 1.0.0
//@title ADS7128 ADC
//@description Eight-channel ADC with I2C control and an on-chip window comparator.
//@protocol i2c
//@tags i2c adc io voltage ic
//@contract-provider SYS_DEVICE_CONTRACT_IO
//@capability pins @one_of [0,1,2,3,4,5,6,7]
//@capability pin-mode @one_of [SYS_IO_MODE_ADC]
//@capability alert-output open-drain active-low optional

//@contract packet_sys_io_reset_t @alias Reset configuration
//@param pin @alias ADC Channel @one_of [0,1,2,3,4,5,6,7]
//@description Reset one ADC channel and clear its comparator configuration.

//@contract packet_sys_io_set_mode_t @alias Configure mode
//@param pin @alias ADC Channel @one_of [0,1,2,3,4,5,6,7]
//@param mode @alias Channel Mode @enum sys_io_mode_e @one_of [SYS_IO_MODE_ADC]
//@description ADS7128 channels are ADC-only; other SYS_IO_MODE values are unavailable.

//@contract packet_sys_io_get_voltage_t @alias Read voltage on pin
//@param pin @alias ADC Channel @one_of [0,1,2,3,4,5,6,7]
//@returns voltage_mV @type uint32_t @unit mV
//@description Read the selected ADC channel using the installed AVDD reference voltage.

//@contract packet_sys_io_configure_intr_t @alias Configure alert
//@param pin @alias ADC Channel @one_of [0,1,2,3,4,5,6,7]
//@param mode @alias Window Mode @enum sys_io_intr_mode_e @one_of [SYS_IO_INTR_ADC_WINDOW_INSIDE,SYS_IO_INTR_ADC_WINDOW_OUTSIDE]
//@param adc_thresh_up_mV @alias Upper Threshold @type uint16_t @unit mV @optional @default 0
//@param adc_thresh_down_mV @alias Lower Threshold @type uint16_t @unit mV @optional @default 0
//@param adc_thresh_hyst_mV @alias Hysteresis @type uint16_t @unit mV @optional @default 0
//@param adc_counter_thresh @alias Event Count Threshold @type uint16_t @optional @default 0
//@description Configure the on-chip ADC window comparator. The optional ALERT pin reports events to a board GPIO.


#define HEADER_packet_sys_device_install_ads7128_t 0x47
typedef struct __packed {
  uint8_t device_id;          //@required @alias Device ID @min 0 @max CONFIG_SYS_DEVICE_MAX_ID
  uint8_t i2c_bus;            //@required @alias I2C Bus @one_of [0,1] @note 0 selects the first logical I2C bus; any non-zero wire value is converted to bus 1 by the decoder
  uint8_t i2c_addr;           //@required @alias I2C Address @min 0x00 @max 0x7F @unit 7-bit-address @note 7-bit I2C address; driver_ads7128.c does not enforce a device-specific address list
  uint8_t intr_pin_device_id; //@group alert-pin @role device_id @alias ALERT GPIO Provider
  uint8_t intr_pin_pin;       //@group alert-pin @role pin @alias ALERT Pin @sentinel SYS_GPIO_NONE @note SYS_GPIO_NONE disables external ALERT reporting; ALERT is open-drain and active-low
  uint8_t intr_pin_mode;      //@group alert-pin @role mode @alias ALERT Pin Mode @ref sys_io_mode_e @one_of [SYS_IO_MODE_INPUT_PULLUP] @note Required when ALERT reporting is enabled
  uint32_t vref_mv;           //@required @alias ADC Reference Voltage @unit mV @min 1 @note AVDD doubles as ADC reference; adapter_ads7128.c rejects zero and firmware has no additional numeric bound
} packet_sys_device_install_ads7128_t;

static inline err_h decoder_packet_sys_device_install_ads7128_t(packet_sys_device_install_ads7128_t* packet) {
  ESP_LOGI(DEC_SYS_DEVICE_INSTALL_TAG, "installing ads7128 (dev %u, i2c bus %u addr 0x%02X, vref %lu mV)", packet->device_id, packet->i2c_bus, packet->i2c_addr, (unsigned long)packet->vref_mv);
  d_ads7128_cfg_t cfg = {.device_id = packet->device_id, .i2c_bus = packet->i2c_bus != 0, .i2c_addr = packet->i2c_addr,
                          .intr_pin = pin_ref_from_wire(packet->intr_pin_device_id, packet->intr_pin_pin, packet->intr_pin_mode), .vref_mv = packet->vref_mv};
  return d_ads7128_create(&cfg);
}

#define SYS_CONTRACTS_DEVICE_ADS7128_PACKET_LIST(X) \
  X(HEADER_packet_sys_device_install_ads7128_t, packet_sys_device_install_ads7128_t, decoder_packet_sys_device_install_ads7128_t)
