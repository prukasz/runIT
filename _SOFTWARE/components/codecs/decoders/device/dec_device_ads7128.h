#pragma once

#include "dec_device_common.h"
#include "../../../devices/device_ads7128/include/device_ads7128.h"

//#device device_ads_7128
//  @title       ADS7128 ADC
//  @description Eight-channel ADC with I2C control and an on-chip window comparator.
//  @protocol    i2c
//  @tags        i2c adc io voltage ic
//  @datasheet   https://www.ti.com/lit/ds/symlink/ads7128.pdf
//  @contract-provider $SYS_DEVICE_CONTRACT_IO

//#self-property PIN
//  @one-of   [0..7]
//  @alias    ADC Channel

//#property PIN-MODE
//  @enum-ref sys_io_mode_e
//  @one-of   [$SYS_IO_MODE_ADC]
//  @default  $SYS_IO_MODE_ADC
//  @alias    Channel Mode

//#property INTR-MODE
//  @enum-ref sys_io_intr_mode_e
//  @one-of   [$SYS_IO_INTR_ADC_WINDOW_INSIDE, $SYS_IO_INTR_ADC_WINDOW_OUTSIDE]
//  @alias    Window Mode

//#contract packet_sys_io_reset_t
//  @alias       Reset configuration
//  @description Reset one ADC channel and clear its comparator configuration.
//  @param pin   @arg PIN

//#contract packet_sys_io_set_mode_t
//  @alias       Configure mode
//  @description ADS7128 channels are ADC-only.
//  @param pin   @arg PIN
//  @param mode  @arg PIN-MODE

//#contract packet_sys_io_get_voltage_t
//  @alias       Read voltage on pin
//  @description Read the selected ADC channel using the installed AVDD reference voltage.
//  @param pin   @arg PIN
//  @returns     voltage_mV

//#contract packet_sys_io_configure_intr_t
//  @alias       Configure alert
//  @description Configure the on-chip ADC window comparator. The optional ALERT pin reports events to a
//               board GPIO.
//  @param pin                @arg PIN
//  @param mode               @arg INTR-MODE
//  @param adc_thresh_up_mV   @alias Upper Threshold
//  @param adc_thresh_down_mV @alias Lower Threshold
//  @param adc_thresh_hyst_mV @alias Hysteresis
//  @param adc_counter_thresh @alias Event Count Threshold

#define HEADER_packet_sys_device_install_ads7128_t 0x47
typedef struct __packed {
  uint8_t device_id;       //@max CONFIG_SYS_DEVICE_MAX_ID
  uint8_t i2c_bus;
  uint8_t i2c_addr;        //@alias I2C Address @one-of [0x10..0x17] @note The resistor on the ADDR pin selects the address at power-up.
  pin_ref_wire_t intr_pin; //@alias ALERT @modes [$SYS_IO_MODE_INPUT, $SYS_IO_MODE_INPUT_PULLUP] @default-mode $SYS_IO_MODE_INPUT_PULLUP
                           //  @note Open-drain, active-low.
  uint32_t vref_mV;        //@alias ADC Reference Voltage @unit mV @min 1 @note The chip's AVDD supply, which is also the ADC reference.
} packet_sys_device_install_ads7128_t;
// Any non-zero i2c_bus selects bus 1; adapter_ads7128.c rejects vref_mV 0.

static inline SE_MUST_USE err_h decoder_packet_sys_device_install_ads7128_t(packet_sys_device_install_ads7128_t* packet) {
  ESP_LOGI(DEC_SYS_DEVICE_INSTALL_TAG, "installing ads7128 (dev %u, i2c bus %u addr 0x%02X, vref %lu mV)", packet->device_id, packet->i2c_bus, packet->i2c_addr, (unsigned long)packet->vref_mV);
  d_ads7128_cfg_t cfg = {.device_id = packet->device_id, .i2c_bus = packet->i2c_bus != 0, .i2c_addr = packet->i2c_addr,
                          .intr_pin = pin_ref_from_wire(packet->intr_pin), .vref_mV = packet->vref_mV};
  err_h err = PIN_REFS_BELOW(cfg.device_id, cfg.intr_pin);
  if (err) return err;
  return d_ads7128_create(&cfg);
}

#define SYS_CONTRACTS_DEVICE_ADS7128_PACKET_LIST(X) \
  X(HEADER_packet_sys_device_install_ads7128_t, packet_sys_device_install_ads7128_t, decoder_packet_sys_device_install_ads7128_t)
