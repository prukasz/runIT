#pragma once
#include "sys_device.h"
#include "sys_error.h"
#include "sys_io.h"

#define ADS7128_TYPE_ID 0x47  /* the byte after 0x00 in a create frame: [0x00][0x47][d_ads7128_cfg_t] */

// The annotations below describe the device to the app (data-structures/devices/*.generated.json,
// grammar: data-structures/auto-annotations/device/device-annotations.md).

//#device device_ads_7128
//  @title       ADS7128 ADC
//  @description Eight-channel ADC with I2C control and an on-chip window comparator.
//  @protocol    i2c
//  @tags        i2c adc io voltage ic
//  @datasheet   https://www.ti.com/lit/ds/symlink/ads7128.pdf
//  @type-id     ADS7128_TYPE_ID
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

/**
 * @brief ADS7128 configuration. Also the wire struct of the create frame (packed, device_id first).
 *
 * The eight channels are exposed as ADC pins 0..7 of the IO contract:
 * sys_io_get_voltage() reads one, sys_io_configure_intr() arms the on-chip
 * window comparator with SYS_IO_INTR_ADC_WINDOW_INSIDE / _OUTSIDE.
 *
 * @warning An ALERT pin that is not wired MUST be spelled `SYS_IO_PIN_NONE_INIT`; omitting the
 *          field zero-fills it to device 0 / pin 0, which is a real pin.
 */
typedef struct __packed {
  uint8_t device_id;       //@max CONFIG_SYS_DEVICE_MAX_ID
  uint8_t i2c_bus;
  uint8_t i2c_addr;        //@alias I2C Address @one-of [0x10..0x17] @note The resistor on the ADDR pin selects the address at power-up.
  sys_io_pin_ref_t intr_pin; //@alias ALERT @modes [$SYS_IO_MODE_INPUT, $SYS_IO_MODE_INPUT_PULLUP] @default-mode $SYS_IO_MODE_INPUT_PULLUP
                           //  @note Open-drain, active-low.
  uint32_t vref_mV;        //@alias ADC Reference Voltage @unit mV @min 1 @note The chip's AVDD supply, which is also the ADC reference.
} d_ads7128_cfg_t;

/** The ADS7128 device class: register with sys_device_register_class(), create with SYS_DEVICE_CREATE(&g_ads7128_class, &cfg). */
extern const sys_device_class_t g_ads7128_class;
