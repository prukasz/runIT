#pragma once
/**
 * @file dec_sys_contracts.h
 * @brief Header-only decoder table for the "System Contracts" packet class (0x01).
 *
 * Wire format handled by this class:
 * @code
 *   [0x01] [0xYY] [ packed payload struct ]
 *    class  packet         sizeof(packet_<name>_t)
 * @endcode
 *
 * The outer class byte (0x01) is consumed by sys_interface_decode(), which then
 * hands the remaining bytes to dec_sys_contracts_decode() with data[0] == 0xYY.
 *
 * Adding a packet is a three-step, single-file change:
 *   1. `#define HEADER_packet_xxx_t 0xYY`
 *   2. declare the `__packed` payload struct + a `decoder_packet_xxx_t()` inline
 *   3. append one `X(...)` row to SYS_CONTRACTS_PACKET_LIST
 */

#include <sdkconfig.h>
#include <stdint.h>
#include "dec_sys_device_install.h"
#include "sys_device.h"
#include "sys_error.h"
#include "sys_interface.h"
#include "sys_io.h"
#include "sys_hbridge.h"
#include "sys_power.h"

//@contract-catalog system @title System contracts @description Device lifecycle, IO, power, and H-bridge operations exposed by the system-contracts interface class.
//@contract-list SYS_CONTRACTS_PACKET_LIST

// dec_sys_device_install.h leaves OWNER set to OWNER_DEC_SYS_DEVICE_INSTALL;
// take it back so this file's own SE_* macros are tagged as dec_sys_contracts.
#undef OWNER
#define OWNER OWNER_DEC_SYS_CONTRACTS

/** @brief ESP log tag used by every decoder in this table. */
#define DEC_SYS_CONTRACTS_TAG "dec_sys_contracts"

// ==========================================================================
// Device Management Packet decoders (0x10 - 0x19)
// ==========================================================================

#define HEADER_packet_sys_device_uninstall_t 0x10
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
} packet_sys_device_uninstall_t;

static inline SE_MUST_USE err_h decoder_packet_sys_device_uninstall_t(packet_sys_device_uninstall_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "uninstalling device %u", packet->device_id);
  return sys_device_user_uninstall(packet->device_id);
}

#define HEADER_packet_sys_device_reset_t 0x11
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
} packet_sys_device_reset_t;

static inline SE_MUST_USE err_h decoder_packet_sys_device_reset_t(packet_sys_device_reset_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "resetting device %u", packet->device_id);
  return sys_device_reset(packet->device_id);
}

#define HEADER_packet_sys_device_suspend_t 0x12
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
} packet_sys_device_suspend_t;

static inline SE_MUST_USE err_h decoder_packet_sys_device_suspend_t(packet_sys_device_suspend_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "suspending device %u", packet->device_id);
  return sys_device_suspend(packet->device_id);
}

#define HEADER_packet_sys_device_resume_t 0x13
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
} packet_sys_device_resume_t;

static inline SE_MUST_USE err_h decoder_packet_sys_device_resume_t(packet_sys_device_resume_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "resuming device %u", packet->device_id);
  return sys_device_resume(packet->device_id);
}

#define HEADER_packet_sys_device_suspend_all_t 0x14
typedef struct __packed {
} packet_sys_device_suspend_all_t;

static inline SE_MUST_USE err_h decoder_packet_sys_device_suspend_all_t(packet_sys_device_suspend_all_t* packet) {
  (void)packet;
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "suspending all devices");
  return sys_device_suspend_all();
}

#define HEADER_packet_sys_device_resume_all_t 0x15
typedef struct __packed {
} packet_sys_device_resume_all_t;

static inline SE_MUST_USE err_h decoder_packet_sys_device_resume_all_t(packet_sys_device_resume_all_t* packet) {
  (void)packet;
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "resuming all devices");
  return sys_device_resume_all();
}

#define HEADER_packet_sys_device_freeze_t 0x16
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
} packet_sys_device_freeze_t;

static inline SE_MUST_USE err_h decoder_packet_sys_device_freeze_t(packet_sys_device_freeze_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "freezing device %u", packet->device_id);
  return sys_device_freeze(packet->device_id);
}

#define HEADER_packet_sys_device_sync_t 0x17
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
} packet_sys_device_sync_t;

static inline SE_MUST_USE err_h decoder_packet_sys_device_sync_t(packet_sys_device_sync_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "syncing device %u", packet->device_id);
  return sys_device_sync(packet->device_id);
}

#define HEADER_packet_sys_device_freeze_all_t 0x18
typedef struct __packed {
} packet_sys_device_freeze_all_t;

static inline SE_MUST_USE err_h decoder_packet_sys_device_freeze_all_t(packet_sys_device_freeze_all_t* packet) {
  (void)packet;
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "freezing all devices");
  return sys_device_freeze_all();
}

#define HEADER_packet_sys_device_sync_all_t 0x19
typedef struct __packed {
} packet_sys_device_sync_all_t;

static inline SE_MUST_USE err_h decoder_packet_sys_device_sync_all_t(packet_sys_device_sync_all_t* packet) {
  (void)packet;
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "syncing all devices");
  return sys_device_sync_all();
}

#define HEADER_packet_sys_device_set_error_handling_t 0x1A
typedef struct __packed {
  uint8_t device_id;                 //@required @alias Device ID
  uint8_t importance;                //@required @alias Importance Level @enum-ref sys_device_importance_e @one-of [$SYS_DEV_IMPORTANCE_NONE, $SYS_DEV_IMPORTANCE_LOW, $SYS_DEV_IMPORTANCE_MEDIUM, $SYS_DEV_IMPORTANCE_HIGH, $SYS_DEV_IMPORTANCE_CRITICAL]
  uint8_t actions[5];                //@required @alias Error-Level Actions @note sys_actions ids, indexed by se_level_e
} packet_sys_device_set_error_handling_t;

static inline SE_MUST_USE err_h decoder_packet_sys_device_set_error_handling_t(packet_sys_device_set_error_handling_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "setting error handling (dev %u): importance=%u, actions=[%u,%u,%u,%u,%u]", packet->device_id,
           packet->importance, packet->actions[0], packet->actions[1], packet->actions[2], packet->actions[3], packet->actions[4]);
  return sys_device_set_error_handling(packet->device_id, (sys_device_importance_e)packet->importance, packet->actions);
}

#define HEADER_packet_sys_device_reset_all_t 0x1B
typedef struct __packed {
} packet_sys_device_reset_all_t;

static inline SE_MUST_USE err_h decoder_packet_sys_device_reset_all_t(packet_sys_device_reset_all_t* packet) {
  (void)packet;
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "resetting all devices");
  return sys_device_reset_all();
}

#define HEADER_packet_sys_device_uninstall_all_t 0x1C
typedef struct __packed {
} packet_sys_device_uninstall_all_t;

static inline SE_MUST_USE err_h decoder_packet_sys_device_uninstall_all_t(packet_sys_device_uninstall_all_t* packet) {
  (void)packet;
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "uninstalling all devices");
  return sys_device_user_uninstall_all();
}

// ==========================================================================
// IO Control Packet decoders (0x20 - 0x2F)
// ==========================================================================

#define HEADER_packet_sys_io_reset_t 0x20
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
  uint8_t pin;       //@required @alias Pin Number
} packet_sys_io_reset_t;

static inline SE_MUST_USE err_h decoder_packet_sys_io_reset_t(packet_sys_io_reset_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "resetting io (dev %u, pin %u)", packet->device_id, packet->pin);
  return sys_io_reset(SYS_IO_REF(packet->device_id, packet->pin));
}

#define HEADER_packet_sys_io_set_mode_t 0x21
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
  uint8_t pin;       //@required @alias Pin Number
  uint8_t mode;      //@required @alias Pin Mode @enum-ref sys_io_mode_e @one-of [$SYS_IO_MODE_INPUT, $SYS_IO_MODE_INPUT_PULLUP, $SYS_IO_MODE_INPUT_PULLDOWN, $SYS_IO_MODE_OUTPUT_PUSH_PULL, $SYS_IO_MODE_OUTPUT_OPEN_DRAIN, $SYS_IO_MODE_OUTPUT_OPEN_DRAIN_PULLUP, $SYS_IO_MODE_PWM, $SYS_IO_MODE_ADC, $SYS_IO_MODE_DAC]
} packet_sys_io_set_mode_t;

static inline SE_MUST_USE err_h decoder_packet_sys_io_set_mode_t(packet_sys_io_set_mode_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "setting io mode %u (dev %u, pin %u)", packet->mode, packet->device_id, packet->pin);
  return sys_io_set_mode(SYS_IO_PIN(packet->device_id, packet->pin, (sys_io_mode_e)packet->mode));
}

#define HEADER_packet_sys_io_set_level_t 0x22
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
  uint8_t pin;       //@required @alias Pin Number
  uint8_t level;     //@required @alias Level
} packet_sys_io_set_level_t;

static inline SE_MUST_USE err_h decoder_packet_sys_io_set_level_t(packet_sys_io_set_level_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "setting io level %u (dev %u, pin %u)", packet->level != 0, packet->device_id, packet->pin);
  return sys_io_set_level(SYS_IO_REF(packet->device_id, packet->pin), packet->level != 0);
}

#define HEADER_packet_sys_io_get_level_t 0x23
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
  uint8_t pin;       //@required @alias Pin Number
} packet_sys_io_get_level_t;

typedef struct __packed {
  uint8_t device_id; //@alias Device ID
  uint8_t pin;       //@alias Pin Number
  uint8_t level;     //@alias Level @min 0 @max 1
} packet_sys_io_get_level_response_t;

static inline SE_MUST_USE err_h decoder_packet_sys_io_get_level_t(packet_sys_io_get_level_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "reading io level (dev %u, pin %u)", packet->device_id, packet->pin);
  bool level = false;
  SE_TRY(sys_io_get_level(SYS_IO_REF(packet->device_id, packet->pin), &level));
  packet_sys_io_get_level_response_t rsp = {.device_id = packet->device_id, .pin = packet->pin, .level = level};
  return sys_interface_respond(&rsp, sizeof(rsp));
}

#define HEADER_packet_sys_io_toggle_t 0x24
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
  uint8_t pin;       //@required @alias Pin Number
} packet_sys_io_toggle_t;

static inline SE_MUST_USE err_h decoder_packet_sys_io_toggle_t(packet_sys_io_toggle_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "toggling io (dev %u, pin %u)", packet->device_id, packet->pin);
  return sys_io_toggle(SYS_IO_REF(packet->device_id, packet->pin));
}

#define HEADER_packet_sys_io_get_voltage_t 0x25
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
  uint8_t pin;       //@required @alias Pin Number
} packet_sys_io_get_voltage_t;

typedef struct __packed {
  uint8_t device_id;  //@alias Device ID
  uint8_t pin;        //@alias Pin Number
  int32_t voltage_mV; //@alias Voltage @unit mV
} packet_sys_io_get_voltage_response_t;

static inline SE_MUST_USE err_h decoder_packet_sys_io_get_voltage_t(packet_sys_io_get_voltage_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "reading io voltage (dev %u, pin %u)", packet->device_id, packet->pin);
  int32_t voltage_mV = 0;
  SE_TRY(sys_io_get_voltage(SYS_IO_REF(packet->device_id, packet->pin), &voltage_mV));
  packet_sys_io_get_voltage_response_t rsp = {.device_id = packet->device_id, .pin = packet->pin, .voltage_mV = voltage_mV};
  return sys_interface_respond(&rsp, sizeof(rsp));
}

#define HEADER_packet_sys_io_set_voltage_t 0x26
typedef struct __packed {
  uint8_t device_id;    //@required @alias Device ID
  uint8_t pin;          //@required @alias Pin Number
  uint32_t voltage_mV;  //@required @alias Voltage @unit mV
} packet_sys_io_set_voltage_t;

static inline SE_MUST_USE err_h decoder_packet_sys_io_set_voltage_t(packet_sys_io_set_voltage_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "setting io voltage %lu mV (dev %u, pin %u)", (unsigned long)packet->voltage_mV, packet->device_id, packet->pin);
  return sys_io_set_voltage(SYS_IO_REF(packet->device_id, packet->pin), packet->voltage_mV);
}

#define HEADER_packet_sys_io_set_pwm_frequency_t 0x27
typedef struct __packed {
  uint8_t device_id;     //@required @alias Device ID
  uint8_t pin;           //@required @alias Pin Number
  uint32_t frequency_Hz; //@required @alias PWM Frequency @unit Hz
} packet_sys_io_set_pwm_frequency_t;

static inline SE_MUST_USE err_h decoder_packet_sys_io_set_pwm_frequency_t(packet_sys_io_set_pwm_frequency_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "setting pwm frequency %lu Hz (dev %u, pin %u)", (unsigned long)packet->frequency_Hz, packet->device_id,
           packet->pin);
  return sys_io_set_pwm_frequency(SYS_IO_REF(packet->device_id, packet->pin), packet->frequency_Hz);
}

#define HEADER_packet_sys_io_set_pwm_duty_t 0x28
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
  uint8_t pin;       //@required @alias Pin Number
  uint32_t duty;     //@required @alias Duty Cycle
} packet_sys_io_set_pwm_duty_t;

static inline SE_MUST_USE err_h decoder_packet_sys_io_set_pwm_duty_t(packet_sys_io_set_pwm_duty_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "setting pwm duty %lu (dev %u, pin %u)", (unsigned long)packet->duty, packet->device_id, packet->pin);
  return sys_io_set_pwm_duty(SYS_IO_REF(packet->device_id, packet->pin), packet->duty);
}

#define HEADER_packet_sys_io_configure_intr_t 0x29
typedef struct __packed {
  uint8_t device_id;          //@required @alias Device ID
  uint8_t pin;                //@required @alias Pin Number
  uint8_t mode;               //@required @alias Interrupt Mode @enum-ref sys_io_intr_mode_e @one-of [$SYS_IO_INTR_DISABLE, $SYS_IO_INTR_MODE_RISING_EDGE, $SYS_IO_INTR_MODE_FALLING_EDGE, $SYS_IO_INTR_MODE_BOTH_EDGES, $SYS_IO_INTR_ADC_WINDOW_OUTSIDE, $SYS_IO_INTR_ADC_WINDOW_INSIDE]
  uint8_t debounce;           //@optional @alias Debounce @note 1 = ignore switch bounce
  uint16_t adc_thresh_up_mV;    //@optional @alias ADC Rising Threshold @unit mV
  uint16_t adc_thresh_down_mV;  //@optional @alias ADC Falling Threshold @unit mV
  uint16_t adc_thresh_hyst_mV;  //@optional @alias ADC Hysteresis @unit mV
  uint16_t adc_counter_thresh;  //@optional @alias ADC Event Counter Threshold
} packet_sys_io_configure_intr_t;

static inline SE_MUST_USE err_h decoder_packet_sys_io_configure_intr_t(packet_sys_io_configure_intr_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "configuring io intr (dev %u, pin %u): mode=%u, debounce=%u", packet->device_id, packet->pin, packet->mode,
           packet->debounce);
  sys_io_intr_config_t config = {
    .mode = (sys_io_intr_mode_e)packet->mode,
    .debounce = packet->debounce != 0,
    .adc = {
      .adc_threshold_up_mV = packet->adc_thresh_up_mV,
      .adc_threshold_down_mV = packet->adc_thresh_down_mV,
      .adc_threshold_hysteresis_mV = packet->adc_thresh_hyst_mV,
      .adc_event_counter_threshold = packet->adc_counter_thresh,
    }
  };
  return sys_io_configure_intr(SYS_IO_REF(packet->device_id, packet->pin), &config);
}

// ==========================================================================
// Power Control Packet decoders (0x30 - 0x3F)
// ==========================================================================

#define HEADER_packet_sys_power_vreg_set_enable_t 0x31
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
  uint8_t state;     //@required @alias Enable State
} packet_sys_power_vreg_set_enable_t;

static inline SE_MUST_USE err_h decoder_packet_sys_power_vreg_set_enable_t(packet_sys_power_vreg_set_enable_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "%s vreg (dev %u)", packet->state != 0 ? "enabling" : "disabling", packet->device_id);
  return sys_power_vreg_set_enable(packet->device_id, packet->state != 0);
}

#define HEADER_packet_sys_power_vreg_set_voltage_t 0x32
typedef struct __packed {
  uint8_t device_id;   //@required @alias Device ID
  uint32_t voltage_mV; //@required @alias Voltage @unit mV
} packet_sys_power_vreg_set_voltage_t;

static inline SE_MUST_USE err_h decoder_packet_sys_power_vreg_set_voltage_t(packet_sys_power_vreg_set_voltage_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "setting vreg voltage %lu mV (dev %u)", (unsigned long)packet->voltage_mV, packet->device_id);
  return sys_power_vreg_set_voltage(packet->device_id, packet->voltage_mV);
}

#define HEADER_packet_sys_power_vreg_set_current_t 0x33
typedef struct __packed {
  uint8_t device_id;   //@required @alias Device ID
  uint32_t current_mA; //@required @alias Current Limit @unit mA
} packet_sys_power_vreg_set_current_t;

static inline SE_MUST_USE err_h decoder_packet_sys_power_vreg_set_current_t(packet_sys_power_vreg_set_current_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "setting vreg current limit %lu mA (dev %u)", (unsigned long)packet->current_mA, packet->device_id);
  return sys_power_vreg_set_current(packet->device_id, packet->current_mA);
}

#define HEADER_packet_sys_power_monitor_get_voltage_t 0x34
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
  uint8_t channel;   //@required @alias Channel
} packet_sys_power_monitor_get_voltage_t;

typedef struct __packed {
  uint8_t device_id;  //@alias Device ID
  uint8_t channel;    //@alias Channel
  int32_t voltage_mV; //@alias Voltage @unit mV
} packet_sys_power_monitor_get_voltage_response_t;

static inline SE_MUST_USE err_h decoder_packet_sys_power_monitor_get_voltage_t(packet_sys_power_monitor_get_voltage_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "reading monitor voltage (dev %u, ch %u)", packet->device_id, packet->channel);
  int32_t voltage_mV = 0;
  SE_TRY(sys_power_monitor_get_voltage(packet->device_id, packet->channel, &voltage_mV));
  packet_sys_power_monitor_get_voltage_response_t rsp = {.device_id = packet->device_id, .channel = packet->channel, .voltage_mV = voltage_mV};
  return sys_interface_respond(&rsp, sizeof(rsp));
}

#define HEADER_packet_sys_power_monitor_get_current_t 0x35
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
  uint8_t channel;   //@required @alias Channel
} packet_sys_power_monitor_get_current_t;

typedef struct __packed {
  uint8_t device_id;  //@alias Device ID
  uint8_t channel;    //@alias Channel
  int32_t current_mA; //@alias Current @unit mA
} packet_sys_power_monitor_get_current_response_t;

static inline SE_MUST_USE err_h decoder_packet_sys_power_monitor_get_current_t(packet_sys_power_monitor_get_current_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "reading monitor current (dev %u, ch %u)", packet->device_id, packet->channel);
  int32_t current_mA = 0;
  SE_TRY(sys_power_monitor_get_current(packet->device_id, packet->channel, &current_mA));
  packet_sys_power_monitor_get_current_response_t rsp = {.device_id = packet->device_id, .channel = packet->channel, .current_mA = current_mA};
  return sys_interface_respond(&rsp, sizeof(rsp));
}

#define HEADER_packet_sys_power_usb_pd_set_t 0x36
typedef struct __packed {
  uint8_t device_id;   //@required @alias Device ID
  uint32_t voltage_mV; //@required @alias Requested Voltage @unit mV
  uint32_t current_mA; //@required @alias Requested Current @unit mA
} packet_sys_power_usb_pd_set_t;

static inline SE_MUST_USE err_h decoder_packet_sys_power_usb_pd_set_t(packet_sys_power_usb_pd_set_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "requesting usb-pd %lu mV / %lu mA (dev %u)", (unsigned long)packet->voltage_mV, (unsigned long)packet->current_mA,
           packet->device_id);
  return sys_power_usb_pd_set(packet->device_id, packet->voltage_mV, packet->current_mA);
}

#define HEADER_packet_sys_power_usb_pd_list_t 0x37
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
} packet_sys_power_usb_pd_list_t;

/* Parallel arrays (scalars only on the wire); entries [0, count) are valid. */
typedef struct __packed {
  uint8_t device_id;                              //@alias Device ID
  uint8_t count;                                  //@alias Option Count @min 0 @max 13
  uint8_t slot[SYS_POWER_USB_PD_OPTIONS_MAX];     //@alias Source Slot
  uint8_t type[SYS_POWER_USB_PD_OPTIONS_MAX];     //@alias Type @enum-ref sys_power_usb_pd_option_type_e @one-of [$SYS_POWER_USB_PD_OPTION_FIXED, $SYS_POWER_USB_PD_OPTION_PPS, $SYS_POWER_USB_PD_OPTION_AVS]
  uint16_t min_mV[SYS_POWER_USB_PD_OPTIONS_MAX];  //@alias Minimum Voltage @unit mV
  uint16_t max_mV[SYS_POWER_USB_PD_OPTIONS_MAX];  //@alias Maximum Voltage @unit mV
  uint16_t max_mA[SYS_POWER_USB_PD_OPTIONS_MAX];  //@alias Maximum Current @unit mA
} packet_sys_power_usb_pd_list_response_t;

static inline SE_MUST_USE err_h decoder_packet_sys_power_usb_pd_list_t(packet_sys_power_usb_pd_list_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "listing usb-pd source capabilities (dev %u)", packet->device_id);
  sys_power_usb_pd_option_t options[SYS_POWER_USB_PD_OPTIONS_MAX];
  uint8_t count = 0;
  SE_TRY(sys_power_usb_pd_list(packet->device_id, options, SYS_POWER_USB_PD_OPTIONS_MAX, &count));
  packet_sys_power_usb_pd_list_response_t rsp = {.device_id = packet->device_id, .count = count};
  for (uint8_t i = 0; i < count; i++) {
    rsp.slot[i] = options[i].slot;
    rsp.type[i] = options[i].type;
    rsp.min_mV[i] = options[i].min_mV;
    rsp.max_mV[i] = options[i].max_mV;
    rsp.max_mA[i] = options[i].max_mA;
  }
  return sys_interface_respond(&rsp, sizeof(rsp));
}

#define HEADER_packet_sys_power_usb_pd_get_limits_t 0x38
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
} packet_sys_power_usb_pd_get_limits_t;

typedef struct __packed {
  uint8_t device_id; //@alias Device ID
  int32_t max_mV;    //@alias Maximum Voltage @unit mV
  int32_t max_mA;    //@alias Maximum Current @unit mA
} packet_sys_power_usb_pd_get_limits_response_t;

static inline SE_MUST_USE err_h decoder_packet_sys_power_usb_pd_get_limits_t(packet_sys_power_usb_pd_get_limits_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "reading usb-pd negotiated limits (dev %u)", packet->device_id);
  int32_t max_mV = 0;
  int32_t max_mA = 0;
  SE_TRY(sys_power_usb_pd_get_limits(packet->device_id, &max_mV, &max_mA));
  packet_sys_power_usb_pd_get_limits_response_t rsp = {.device_id = packet->device_id, .max_mV = max_mV, .max_mA = max_mA};
  return sys_interface_respond(&rsp, sizeof(rsp));
}

#define HEADER_packet_sys_power_monitor_set_alert_t 0x39
typedef struct __packed {
  uint8_t device_id;    //@required @alias Device ID
  uint8_t channel;      //@required @alias Channel
  uint8_t alert;        //@required @alias Alert @enum-ref sys_power_events_e @one-of [$SYS_PWR_EVENT_OCP_WARNING, $SYS_PWR_EVENT_OCP_CRITICAL]
  int32_t threshold_mA; //@required @alias Current Threshold @unit mA
} packet_sys_power_monitor_set_alert_t;

static inline SE_MUST_USE err_h decoder_packet_sys_power_monitor_set_alert_t(packet_sys_power_monitor_set_alert_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "power monitor alert %u at %ld mA (dev %u, ch %u)", packet->alert, (long)packet->threshold_mA, packet->device_id,
           packet->channel);
  return sys_power_monitor_set_alert(packet->device_id, packet->channel, (sys_power_events_e)packet->alert, packet->threshold_mA);
}

#define HEADER_packet_sys_power_get_status_t 0x3C
typedef struct __packed {
  uint8_t reserved; //@optional @alias Reserved @note always 0
} packet_sys_power_get_status_t;

typedef struct __packed {
  uint8_t source;        //@alias Power Source @enum-ref sys_power_source_e @one-of [$SYS_POWER_SOURCE_UNKNOWN, $SYS_POWER_SOURCE_USB_PD, $SYS_POWER_SOURCE_PSU, $SYS_POWER_SOURCE_BATTERY]
  int32_t input_mV;      //@alias Input Voltage @unit mV
  int32_t input_mA;      //@alias Input Current @unit mA
  uint32_t source_mV;    //@alias Budget Voltage @unit mV
  uint32_t source_mA;    //@alias Budget Current @unit mA
  uint32_t budget_mW;    //@alias Power Budget @unit mW
  uint32_t allocated_mW; //@alias Allocated Power @unit mW
  uint8_t battery_pct;   //@alias Battery Charge @unit % @note 255 = unknown or no battery
} packet_sys_power_get_status_response_t;

static inline SE_MUST_USE err_h decoder_packet_sys_power_get_status_t(packet_sys_power_get_status_t* packet) {
  (void)packet;
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "reading power status");
  sys_power_status_t status;
  SE_TRY(sys_power_get_status(&status));
  packet_sys_power_get_status_response_t rsp = {
      .source = status.source,
      .input_mV = status.input_mV,
      .input_mA = status.input_mA,
      .source_mV = status.source_mV,
      .source_mA = status.source_mA,
      .budget_mW = status.budget_mW,
      .allocated_mW = status.allocated_mW,
      .battery_pct = status.battery_pct,
  };
  return sys_interface_respond(&rsp, sizeof(rsp));
}

#define HEADER_packet_sys_power_set_response_t 0x3D
typedef struct __packed {
  uint8_t event;    //@required @alias Event @enum-ref sys_power_events_e @one-of [$SYS_PWR_EVENT_OVP, $SYS_PWR_EVENT_UVP, $SYS_PWR_EVENT_SPC, $SYS_PWR_EVENT_OCP_WARNING, $SYS_PWR_EVENT_OCP_CRITICAL, $SYS_PWR_EVENT_OTP, $SYS_PWR_EVENT_BATTERY_LOW, $SYS_PWR_EVENT_BATTERY_CRITICAL, $SYS_PWR_EVENT_BUDGET_EXCEEDED, $SYS_PWR_EVENT_SOURCE_CHANGED]
  uint8_t response; //@required @alias Response @enum-ref sys_power_response_e @one-of [$SYS_POWER_RESPONSE_NOTIFY, $SYS_POWER_RESPONSE_DISABLE, $SYS_POWER_RESPONSE_RESET, $SYS_POWER_RESPONSE_SAFE_STATE]
} packet_sys_power_set_response_t;

static inline SE_MUST_USE err_h decoder_packet_sys_power_set_response_t(packet_sys_power_set_response_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "power event %u response -> %u", packet->event, packet->response);
  return sys_power_set_response((sys_power_events_e)packet->event, (sys_power_response_e)packet->response);
}

// ===========================================================================
// H-Bridge Control Packet decoders (0x50 - 0x56)
// ===========================================================================

#define HEADER_packet_sys_hbridge_set_mode_t 0x50
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
  uint8_t channel;   //@required @alias Channel
  uint8_t mode;      //@required @alias H-Bridge Mode @enum-ref sys_hbridge_mode_e @one-of [$SYS_HBRIDGE_MODE_FULL, $SYS_HBRIDGE_MODE_HALF_HIGH, $SYS_HBRIDGE_MODE_HALF_LOW]
} packet_sys_hbridge_set_mode_t;

static inline SE_MUST_USE err_h decoder_packet_sys_hbridge_set_mode_t(packet_sys_hbridge_set_mode_t* packet) {
  return sys_hbridge_set_mode(packet->device_id, packet->channel, (sys_hbridge_mode_e)packet->mode);
}

#define HEADER_packet_sys_hbridge_set_drive_t 0x51
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
  uint8_t channel;   //@required @alias Channel
  float magnitude;   //@required @alias Drive Magnitude
} packet_sys_hbridge_set_drive_t;

static inline SE_MUST_USE err_h decoder_packet_sys_hbridge_set_drive_t(packet_sys_hbridge_set_drive_t* packet) {
  return sys_hbridge_set_drive(packet->device_id, packet->channel, packet->magnitude);
}

#define HEADER_packet_sys_hbridge_brake_t 0x52
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
  uint8_t channel;   //@required @alias Channel
} packet_sys_hbridge_brake_t;

static inline SE_MUST_USE err_h decoder_packet_sys_hbridge_brake_t(packet_sys_hbridge_brake_t* packet) {
  return sys_hbridge_brake(packet->device_id, packet->channel);
}

#define HEADER_packet_sys_hbridge_coast_t 0x53
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
  uint8_t channel;   //@required @alias Channel
} packet_sys_hbridge_coast_t;

static inline SE_MUST_USE err_h decoder_packet_sys_hbridge_coast_t(packet_sys_hbridge_coast_t* packet) {
  return sys_hbridge_coast(packet->device_id, packet->channel);
}

#define HEADER_packet_sys_hbridge_set_current_limit_t 0x54
typedef struct __packed {
  uint8_t device_id;  //@required @alias Device ID
  uint8_t channel;    //@required @alias Channel
  uint32_t limit_mA;  //@required @alias Current Limit @unit mA
} packet_sys_hbridge_set_current_limit_t;

static inline SE_MUST_USE err_h decoder_packet_sys_hbridge_set_current_limit_t(packet_sys_hbridge_set_current_limit_t* packet) {
  return sys_hbridge_set_current_limit_mA(packet->device_id, packet->channel, packet->limit_mA);
}

#define HEADER_packet_sys_hbridge_clear_fault_t 0x56
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
  uint8_t channel;   //@required @alias Channel
} packet_sys_hbridge_clear_fault_t;

static inline SE_MUST_USE err_h decoder_packet_sys_hbridge_clear_fault_t(packet_sys_hbridge_clear_fault_t* packet) {
  return sys_hbridge_clear_fault(packet->device_id, packet->channel);
}

// ==========================================================================
// Central packet list - X-macro expanded into the class dispatcher below
// ==========================================================================
// Device installation packets (0x40 - 0x47) live in dec_sys_device_install.h;
// SYS_CONTRACTS_INSTALL_PACKET_LIST(X) is folded in below rather than repeated here.

#define SYS_CONTRACTS_PACKET_LIST(X)                                                                                                          \
  X(HEADER_packet_sys_device_uninstall_t, packet_sys_device_uninstall_t, decoder_packet_sys_device_uninstall_t)                               \
  X(HEADER_packet_sys_device_reset_t, packet_sys_device_reset_t, decoder_packet_sys_device_reset_t)                                           \
  X(HEADER_packet_sys_device_suspend_t, packet_sys_device_suspend_t, decoder_packet_sys_device_suspend_t)                                     \
  X(HEADER_packet_sys_device_resume_t, packet_sys_device_resume_t, decoder_packet_sys_device_resume_t)                                        \
  X(HEADER_packet_sys_device_suspend_all_t, packet_sys_device_suspend_all_t, decoder_packet_sys_device_suspend_all_t)                         \
  X(HEADER_packet_sys_device_resume_all_t, packet_sys_device_resume_all_t, decoder_packet_sys_device_resume_all_t)                            \
  X(HEADER_packet_sys_device_freeze_t, packet_sys_device_freeze_t, decoder_packet_sys_device_freeze_t)                                        \
  X(HEADER_packet_sys_device_sync_t, packet_sys_device_sync_t, decoder_packet_sys_device_sync_t)                                              \
  X(HEADER_packet_sys_device_freeze_all_t, packet_sys_device_freeze_all_t, decoder_packet_sys_device_freeze_all_t)                            \
  X(HEADER_packet_sys_device_sync_all_t, packet_sys_device_sync_all_t, decoder_packet_sys_device_sync_all_t)                                  \
  X(HEADER_packet_sys_device_set_error_handling_t, packet_sys_device_set_error_handling_t, decoder_packet_sys_device_set_error_handling_t)     \
  X(HEADER_packet_sys_device_reset_all_t, packet_sys_device_reset_all_t, decoder_packet_sys_device_reset_all_t)                                 \
  X(HEADER_packet_sys_device_uninstall_all_t, packet_sys_device_uninstall_all_t, decoder_packet_sys_device_uninstall_all_t)                     \
  X(HEADER_packet_sys_io_reset_t, packet_sys_io_reset_t, decoder_packet_sys_io_reset_t)                                                       \
  X(HEADER_packet_sys_io_set_mode_t, packet_sys_io_set_mode_t, decoder_packet_sys_io_set_mode_t)                                              \
  X(HEADER_packet_sys_io_set_level_t, packet_sys_io_set_level_t, decoder_packet_sys_io_set_level_t)                                           \
  X(HEADER_packet_sys_io_get_level_t, packet_sys_io_get_level_t, decoder_packet_sys_io_get_level_t)                                           \
  X(HEADER_packet_sys_io_toggle_t, packet_sys_io_toggle_t, decoder_packet_sys_io_toggle_t)                                                    \
  X(HEADER_packet_sys_io_get_voltage_t, packet_sys_io_get_voltage_t, decoder_packet_sys_io_get_voltage_t)                                     \
  X(HEADER_packet_sys_io_set_voltage_t, packet_sys_io_set_voltage_t, decoder_packet_sys_io_set_voltage_t)                                     \
  X(HEADER_packet_sys_io_set_pwm_frequency_t, packet_sys_io_set_pwm_frequency_t, decoder_packet_sys_io_set_pwm_frequency_t)                   \
  X(HEADER_packet_sys_io_set_pwm_duty_t, packet_sys_io_set_pwm_duty_t, decoder_packet_sys_io_set_pwm_duty_t)                                  \
  X(HEADER_packet_sys_io_configure_intr_t, packet_sys_io_configure_intr_t, decoder_packet_sys_io_configure_intr_t)                               \
  X(HEADER_packet_sys_power_vreg_set_enable_t, packet_sys_power_vreg_set_enable_t, decoder_packet_sys_power_vreg_set_enable_t)                                  \
  X(HEADER_packet_sys_power_vreg_set_voltage_t, packet_sys_power_vreg_set_voltage_t, decoder_packet_sys_power_vreg_set_voltage_t)                               \
  X(HEADER_packet_sys_power_vreg_set_current_t, packet_sys_power_vreg_set_current_t, decoder_packet_sys_power_vreg_set_current_t)                               \
  X(HEADER_packet_sys_power_monitor_get_voltage_t, packet_sys_power_monitor_get_voltage_t, decoder_packet_sys_power_monitor_get_voltage_t)    \
  X(HEADER_packet_sys_power_monitor_get_current_t, packet_sys_power_monitor_get_current_t, decoder_packet_sys_power_monitor_get_current_t)    \
  X(HEADER_packet_sys_power_usb_pd_set_t, packet_sys_power_usb_pd_set_t, decoder_packet_sys_power_usb_pd_set_t)                               \
  X(HEADER_packet_sys_power_usb_pd_list_t, packet_sys_power_usb_pd_list_t, decoder_packet_sys_power_usb_pd_list_t)                            \
  X(HEADER_packet_sys_power_usb_pd_get_limits_t, packet_sys_power_usb_pd_get_limits_t, decoder_packet_sys_power_usb_pd_get_limits_t)            \
  X(HEADER_packet_sys_power_monitor_set_alert_t, packet_sys_power_monitor_set_alert_t, decoder_packet_sys_power_monitor_set_alert_t)          \
  X(HEADER_packet_sys_power_get_status_t, packet_sys_power_get_status_t, decoder_packet_sys_power_get_status_t)                      \
  X(HEADER_packet_sys_power_set_response_t, packet_sys_power_set_response_t, decoder_packet_sys_power_set_response_t)                \
  X(HEADER_packet_sys_hbridge_set_mode_t, packet_sys_hbridge_set_mode_t, decoder_packet_sys_hbridge_set_mode_t)                               \
  X(HEADER_packet_sys_hbridge_set_drive_t, packet_sys_hbridge_set_drive_t, decoder_packet_sys_hbridge_set_drive_t)                            \
  X(HEADER_packet_sys_hbridge_brake_t, packet_sys_hbridge_brake_t, decoder_packet_sys_hbridge_brake_t)                                       \
  X(HEADER_packet_sys_hbridge_coast_t, packet_sys_hbridge_coast_t, decoder_packet_sys_hbridge_coast_t)                                       \
  X(HEADER_packet_sys_hbridge_set_current_limit_t, packet_sys_hbridge_set_current_limit_t, decoder_packet_sys_hbridge_set_current_limit_t)   \
  X(HEADER_packet_sys_hbridge_clear_fault_t, packet_sys_hbridge_clear_fault_t, decoder_packet_sys_hbridge_clear_fault_t)                     \
  SYS_CONTRACTS_INSTALL_PACKET_LIST(X)

#define SYS_CONTRACTS_DECODE_CASE(header, packet_type, decoder_func)                    \
  case header: {                                                                        \
    packet_type packet;                                                                 \
    SE_TRY(convert_to_packet(data + 1, len - 1, &packet, sizeof(packet_type)));   \
    return decoder_func(&packet);                                                       \
  }

/**
 * @brief Class handler for RX_PACKET_CLASS_SYS_CONTRACTS (0x01).
 *
 * Dispatches on the packet header byte (0xYY) and copies the remaining bytes into
 * the matching `__packed` payload struct before invoking that packet's decoder.
 *
 * @param data Frame bytes with the class byte already stripped - data[0] is 0xYY.
 * @param len Number of bytes available at @p data.
 * @return err_h NULL on success, ERR_INTERFACE_UNKNOWN_PACKET for an unmapped
 *               header, or the decoder's own error chain.
 *
 * @note Registered automatically by sys_interface_init(); not normally called directly.
 *
 * Example - toggle pin 4 on device 3 over BLE: `01 24 03 04`
 */
static inline SE_MUST_USE err_h dec_sys_contracts_decode(const uint8_t* data, size_t len) {
  if (len == 0) {
    SE_FAIL(ERR_INTERFACE_SHORT_FRAME, .got = 0, .need = 1);
  }

  switch (data[0]) {
    SYS_CONTRACTS_PACKET_LIST(SYS_CONTRACTS_DECODE_CASE)
    default:
      ESP_LOGW(DEC_SYS_CONTRACTS_TAG, "unknown packet header 0x%02X", data[0]);
      SE_FAIL(ERR_INTERFACE_UNKNOWN_PACKET, .class_header = CONFIG_RX_PACKET_CLASS_SYS_CONTRACTS, .packet_header = data[0]);
  }
}
