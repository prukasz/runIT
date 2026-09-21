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

#include <stdint.h>
#include "dec_sys_device_install.h"
#include "sys_device.h"
#include "sys_error.h"
#include "sys_interface.h"
#include "sys_io.h"
#include "sys_power.h"

// dec_sys_device_install.h leaves OWNER set to OWNER_DEC_SYS_DEVICE_INSTALL;
// take it back so this file's own SE_* macros are tagged as dec_sys_contracts.
#undef OWNER
#define OWNER OWNER_DEC_SYS_CONTRACTS

/** @brief Class header byte identifying this decoder table on the wire. */
#define SYS_CONTRACTS_CLASS_HEADER 0x01

/** @brief ESP log tag used by every decoder in this table. */
#define DEC_SYS_CONTRACTS_TAG "dec_sys_contracts"

// ==========================================================================
// Device Management Packet decoders (0x10 - 0x19)
// ==========================================================================

#define HEADER_packet_sys_device_uninstall_t 0x10
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
} packet_sys_device_uninstall_t;

static inline err_h decoder_packet_sys_device_uninstall_t(packet_sys_device_uninstall_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "uninstalling device %u", packet->device_id);
  return sys_device_uninstall(packet->device_id);
}

#define HEADER_packet_sys_device_reset_t 0x11
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
} packet_sys_device_reset_t;

static inline err_h decoder_packet_sys_device_reset_t(packet_sys_device_reset_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "resetting device %u", packet->device_id);
  return sys_device_reset(packet->device_id);
}

#define HEADER_packet_sys_device_suspend_t 0x12
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
} packet_sys_device_suspend_t;

static inline err_h decoder_packet_sys_device_suspend_t(packet_sys_device_suspend_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "suspending device %u", packet->device_id);
  return sys_device_suspend(packet->device_id);
}

#define HEADER_packet_sys_device_resume_t 0x13
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
} packet_sys_device_resume_t;

static inline err_h decoder_packet_sys_device_resume_t(packet_sys_device_resume_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "resuming device %u", packet->device_id);
  return sys_device_resume(packet->device_id);
}

#define HEADER_packet_sys_device_suspend_all_t 0x14
typedef struct __packed {
} packet_sys_device_suspend_all_t;

static inline err_h decoder_packet_sys_device_suspend_all_t(packet_sys_device_suspend_all_t* packet) {
  (void)packet;
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "suspending all devices");
  return sys_device_suspend_all();
}

#define HEADER_packet_sys_device_resume_all_t 0x15
typedef struct __packed {
} packet_sys_device_resume_all_t;

static inline err_h decoder_packet_sys_device_resume_all_t(packet_sys_device_resume_all_t* packet) {
  (void)packet;
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "resuming all devices");
  return sys_device_resume_all();
}

#define HEADER_packet_sys_device_freeze_t 0x16
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
} packet_sys_device_freeze_t;

static inline err_h decoder_packet_sys_device_freeze_t(packet_sys_device_freeze_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "freezing device %u", packet->device_id);
  return sys_device_freeze(packet->device_id);
}

#define HEADER_packet_sys_device_sync_t 0x17
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
} packet_sys_device_sync_t;

static inline err_h decoder_packet_sys_device_sync_t(packet_sys_device_sync_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "syncing device %u", packet->device_id);
  return sys_device_sync(packet->device_id);
}

#define HEADER_packet_sys_device_freeze_all_t 0x18
typedef struct __packed {
} packet_sys_device_freeze_all_t;

static inline err_h decoder_packet_sys_device_freeze_all_t(packet_sys_device_freeze_all_t* packet) {
  (void)packet;
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "freezing all devices");
  return sys_device_freeze_all();
}

#define HEADER_packet_sys_device_sync_all_t 0x19
typedef struct __packed {
} packet_sys_device_sync_all_t;

static inline err_h decoder_packet_sys_device_sync_all_t(packet_sys_device_sync_all_t* packet) {
  (void)packet;
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "syncing all devices");
  return sys_device_sync_all();
}

#define HEADER_packet_sys_device_set_error_handling_t 0x1A
typedef struct __packed {
  uint8_t device_id;                 //@required @alias Device ID
  uint8_t importance;                //@required @alias Importance Level @ref sys_device_importance_e
  uint8_t actions[5];                //@required @alias Error-Level Actions @note sys_actions ids, indexed by sys_device_err_level_e
} packet_sys_device_set_error_handling_t;

static inline err_h decoder_packet_sys_device_set_error_handling_t(packet_sys_device_set_error_handling_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "setting error handling (dev %u): importance=%u, actions=[%u,%u,%u,%u,%u]", packet->device_id,
           packet->importance, packet->actions[0], packet->actions[1], packet->actions[2], packet->actions[3], packet->actions[4]);
  return sys_device_set_error_handling(packet->device_id, (sys_device_importance_e)packet->importance, packet->actions);
}

// ==========================================================================
// IO Control Packet decoders (0x20 - 0x2F)
// ==========================================================================

#define HEADER_packet_sys_io_reset_t 0x20
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
  uint8_t pin;       //@required @alias Pin Number
} packet_sys_io_reset_t;

static inline err_h decoder_packet_sys_io_reset_t(packet_sys_io_reset_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "resetting io (dev %u, pin %u)", packet->device_id, packet->pin);
  return sys_io_reset(packet->device_id, packet->pin);
}

#define HEADER_packet_sys_io_set_mode_t 0x21
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
  uint8_t pin;       //@required @alias Pin Number
  uint8_t mode;      //@required @alias Pin Mode @ref sys_io_mode_e
} packet_sys_io_set_mode_t;

static inline err_h decoder_packet_sys_io_set_mode_t(packet_sys_io_set_mode_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "setting io mode %u (dev %u, pin %u)", packet->mode, packet->device_id, packet->pin);
  return sys_io_set_mode(packet->device_id, packet->pin, (sys_io_mode_e)packet->mode);
}

#define HEADER_packet_sys_io_set_level_t 0x22
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
  uint8_t pin;       //@required @alias Pin Number
  uint8_t level;     //@required @alias Level
} packet_sys_io_set_level_t;

static inline err_h decoder_packet_sys_io_set_level_t(packet_sys_io_set_level_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "setting io level %u (dev %u, pin %u)", packet->level != 0, packet->device_id, packet->pin);
  return sys_io_set_level(packet->device_id, packet->pin, packet->level != 0);
}

#define HEADER_packet_sys_io_get_level_t 0x23
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
  uint8_t pin;       //@required @alias Pin Number
} packet_sys_io_get_level_t;

static inline err_h decoder_packet_sys_io_get_level_t(packet_sys_io_get_level_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "reading io level (dev %u, pin %u)", packet->device_id, packet->pin);
  bool level = false;
  err_h err = sys_io_get_level(packet->device_id, packet->pin, &level);
  if (SE_IS_OK(err)) {
    ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "sys_io_get_level (dev %u, pin %u): level=%d", packet->device_id, packet->pin, level);
  }
  return err;
}

#define HEADER_packet_sys_io_toggle_t 0x24
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
  uint8_t pin;       //@required @alias Pin Number
} packet_sys_io_toggle_t;

static inline err_h decoder_packet_sys_io_toggle_t(packet_sys_io_toggle_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "toggling io (dev %u, pin %u)", packet->device_id, packet->pin);
  return sys_io_toggle(packet->device_id, packet->pin);
}

#define HEADER_packet_sys_io_get_voltage_t 0x25
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
  uint8_t pin;       //@required @alias Pin Number
} packet_sys_io_get_voltage_t;

static inline err_h decoder_packet_sys_io_get_voltage_t(packet_sys_io_get_voltage_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "reading io voltage (dev %u, pin %u)", packet->device_id, packet->pin);
  uint32_t out_mV = 0;
  err_h err = sys_io_get_voltage(packet->device_id, packet->pin, &out_mV);
  if (SE_IS_OK(err)) {
    ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "sys_io_get_voltage (dev %u, pin %u): mV=%lu", packet->device_id, packet->pin, (unsigned long)out_mV);
  }
  return err;
}

#define HEADER_packet_sys_io_set_voltage_t 0x26
typedef struct __packed {
  uint8_t device_id;    //@required @alias Device ID
  uint8_t pin;          //@required @alias Pin Number
  uint32_t voltage_mV;  //@required @alias Voltage @unit mV
} packet_sys_io_set_voltage_t;

static inline err_h decoder_packet_sys_io_set_voltage_t(packet_sys_io_set_voltage_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "setting io voltage %lu mV (dev %u, pin %u)", (unsigned long)packet->voltage_mV, packet->device_id, packet->pin);
  return sys_io_set_voltage(packet->device_id, packet->pin, packet->voltage_mV);
}

#define HEADER_packet_sys_io_set_pwm_frequency_t 0x27
typedef struct __packed {
  uint8_t device_id;     //@required @alias Device ID
  uint8_t pin;           //@required @alias Pin Number
  uint32_t frequency_HZ; //@required @alias PWM Frequency @unit Hz
} packet_sys_io_set_pwm_frequency_t;

static inline err_h decoder_packet_sys_io_set_pwm_frequency_t(packet_sys_io_set_pwm_frequency_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "setting pwm frequency %lu Hz (dev %u, pin %u)", (unsigned long)packet->frequency_HZ, packet->device_id,
           packet->pin);
  return sys_io_set_pwm_frequency(packet->device_id, packet->pin, packet->frequency_HZ);
}

#define HEADER_packet_sys_io_set_pwm_duty_t 0x28
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
  uint8_t pin;       //@required @alias Pin Number
  uint32_t duty;     //@required @alias Duty Cycle
} packet_sys_io_set_pwm_duty_t;

static inline err_h decoder_packet_sys_io_set_pwm_duty_t(packet_sys_io_set_pwm_duty_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "setting pwm duty %lu (dev %u, pin %u)", (unsigned long)packet->duty, packet->device_id, packet->pin);
  return sys_io_set_pwm_duty(packet->device_id, packet->pin, packet->duty);
}

#define HEADER_packet_sys_io_configure_intr_t 0x29
typedef struct __packed {
  uint8_t device_id;          //@required @alias Device ID
  uint8_t pin;                //@required @alias Pin Number
  uint8_t mode;               //@required @alias Interrupt Mode @ref sys_io_intr_mode_e
  uint16_t route_mask;        //@required @alias Route Mask @note bitmask
  uint8_t static_action_id;   //@optional @alias Static Action ID @sentinel 0
  uint8_t dynamic_action_id;  //@optional @alias Dynamic Action ID @sentinel 0
  uint16_t adc_thresh_up_mV;    //@optional @alias ADC Rising Threshold @unit mV
  uint16_t adc_thresh_down_mV;  //@optional @alias ADC Falling Threshold @unit mV
  uint16_t adc_thresh_hyst_mV;  //@optional @alias ADC Hysteresis @unit mV
  uint16_t adc_counter_thresh;  //@optional @alias ADC Event Counter Threshold
} packet_sys_io_configure_intr_t;

static inline err_h decoder_packet_sys_io_configure_intr_t(packet_sys_io_configure_intr_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "configuring io intr (dev %u, pin %u): mode=%u, route_mask=0x%04X, static_action=%u, dynamic_action=%u",
           packet->device_id, packet->pin, packet->mode, packet->route_mask, packet->static_action_id, packet->dynamic_action_id);
  sys_io_intr_config_t config = {
    .mode = (sys_io_intr_mode_e)packet->mode,
    .route_mask = packet->route_mask,
    .static_action_id = packet->static_action_id,
    .dynamic_action_id = packet->dynamic_action_id,
    .own_func = { .own_func = NULL, .device_handle = NULL },
    .adc = {
      .adc_threshold_up_mV = packet->adc_thresh_up_mV,
      .adc_threshold_down_mV = packet->adc_thresh_down_mV,
      .adc_threshold_hysteresis_mV = packet->adc_thresh_hyst_mV,
      .adc_event_counter_threshold = packet->adc_counter_thresh,
    }
  };
  return sys_io_configure_intr(packet->device_id, packet->pin, &config);
}

// ==========================================================================
// Power Control Packet decoders (0x30 - 0x3F)
// ==========================================================================

#define HEADER_packet_sys_power_budget_update_source_t 0x30
typedef struct __packed {
  uint32_t max_mV; //@required @alias Max Voltage @unit mV
  uint32_t max_mA; //@required @alias Max Current @unit mA
} packet_sys_power_budget_update_source_t;

static inline err_h decoder_packet_sys_power_budget_update_source_t(packet_sys_power_budget_update_source_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "updating power budget source to %lu mV / %lu mA", (unsigned long)packet->max_mV, (unsigned long)packet->max_mA);
  return sys_power_budget_update_source(packet->max_mV, packet->max_mA);
}

#define HEADER_packet_sys_vreg_set_enable_t 0x31
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
  uint8_t state;     //@required @alias Enable State
} packet_sys_vreg_set_enable_t;

static inline err_h decoder_packet_sys_vreg_set_enable_t(packet_sys_vreg_set_enable_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "%s vreg (dev %u)", packet->state != 0 ? "enabling" : "disabling", packet->device_id);
  return sys_vreg_set_enable(packet->device_id, packet->state != 0);
}

#define HEADER_packet_sys_vreg_set_voltage_t 0x32
typedef struct __packed {
  uint8_t device_id;   //@required @alias Device ID
  uint32_t voltage_mV; //@required @alias Voltage @unit mV
} packet_sys_vreg_set_voltage_t;

static inline err_h decoder_packet_sys_vreg_set_voltage_t(packet_sys_vreg_set_voltage_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "setting vreg voltage %lu mV (dev %u)", (unsigned long)packet->voltage_mV, packet->device_id);
  return sys_vreg_set_voltage(packet->device_id, packet->voltage_mV);
}

#define HEADER_packet_sys_vreg_set_current_t 0x33
typedef struct __packed {
  uint8_t device_id;   //@required @alias Device ID
  uint32_t current_mA; //@required @alias Current Limit @unit mA
} packet_sys_vreg_set_current_t;

static inline err_h decoder_packet_sys_vreg_set_current_t(packet_sys_vreg_set_current_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "setting vreg current limit %lu mA (dev %u)", (unsigned long)packet->current_mA, packet->device_id);
  return sys_vreg_set_current(packet->device_id, packet->current_mA);
}

#define HEADER_packet_sys_power_monitor_get_voltage_t 0x34
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
  uint8_t channel;   //@required @alias Channel
} packet_sys_power_monitor_get_voltage_t;

static inline err_h decoder_packet_sys_power_monitor_get_voltage_t(packet_sys_power_monitor_get_voltage_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "reading monitor voltage (dev %u, ch %u)", packet->device_id, packet->channel);
  int32_t out_mV = 0;
  err_h err = sys_power_monitor_get_voltage(packet->device_id, packet->channel, &out_mV);
  if (SE_IS_OK(err)) {
    ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "sys_power_monitor_get_voltage (dev %u, ch %u): mV=%ld", packet->device_id, packet->channel, (long)out_mV);
  }
  return err;
}

#define HEADER_packet_sys_power_monitor_get_current_t 0x35
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
  uint8_t channel;   //@required @alias Channel
} packet_sys_power_monitor_get_current_t;

static inline err_h decoder_packet_sys_power_monitor_get_current_t(packet_sys_power_monitor_get_current_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "reading monitor current (dev %u, ch %u)", packet->device_id, packet->channel);
  int32_t out_mA = 0;
  err_h err = sys_power_monitor_get_current(packet->device_id, packet->channel, &out_mA);
  if (SE_IS_OK(err)) {
    ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "sys_power_monitor_get_current (dev %u, ch %u): mA=%ld", packet->device_id, packet->channel, (long)out_mA);
  }
  return err;
}

#define HEADER_packet_sys_power_usb_pd_set_t 0x36
typedef struct __packed {
  uint8_t device_id;   //@required @alias Device ID
  uint32_t voltage_mV; //@required @alias Requested Voltage @unit mV
  uint32_t current_mA; //@required @alias Requested Current @unit mA
} packet_sys_power_usb_pd_set_t;

static inline err_h decoder_packet_sys_power_usb_pd_set_t(packet_sys_power_usb_pd_set_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "requesting usb-pd %lu mV / %lu mA (dev %u)", (unsigned long)packet->voltage_mV, (unsigned long)packet->current_mA,
           packet->device_id);
  return sys_power_usb_pd_set(packet->device_id, packet->voltage_mV, packet->current_mA);
}

#define HEADER_packet_sys_power_usb_pd_list_t 0x37
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
} packet_sys_power_usb_pd_list_t;

static inline err_h decoder_packet_sys_power_usb_pd_list_t(packet_sys_power_usb_pd_list_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "listing usb-pd source capabilities (dev %u)", packet->device_id);
  return sys_power_usb_pd_list(packet->device_id);
}

#define HEADER_packet_sys_power_usb_pd_get_limits_t 0x38
typedef struct __packed {
  uint8_t device_id; //@required @alias Device ID
} packet_sys_power_usb_pd_get_limits_t;

static inline err_h decoder_packet_sys_power_usb_pd_get_limits_t(packet_sys_power_usb_pd_get_limits_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "reading usb-pd negotiated limits (dev %u)", packet->device_id);
  uint32_t out_mV = 0;
  uint32_t out_mA = 0;
  err_h err = sys_power_usb_pd_get_limits(packet->device_id, &out_mV, &out_mA);
  if (SE_IS_OK(err)) {
    ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "sys_power_usb_pd_get_limits (dev %u): mV=%lu, mA=%lu", packet->device_id, (unsigned long)out_mV,
             (unsigned long)out_mA);
  }
  return err;
}

#define HEADER_packet_sys_power_monitor_add_callback_t 0x39
typedef struct __packed {
  uint8_t device_id;         //@required @alias Device ID
  uint8_t channel;           //@required @alias Channel
  int32_t trigger_value;     //@required @alias Trigger Value
  uint8_t on_event;          //@required @alias Event @ref sys_power_events_e
  uint16_t route_mask;       //@required @alias Route Mask @note bitmask
  uint8_t static_action_id;  //@optional @alias Static Action ID @sentinel 0
  uint8_t dynamic_action_id; //@optional @alias Dynamic Action ID @sentinel 0
} packet_sys_power_monitor_add_callback_t;

static inline err_h decoder_packet_sys_power_monitor_add_callback_t(packet_sys_power_monitor_add_callback_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "adding power monitor cb (dev %u, ch %u): val=%ld, event=%u, route=0x%04X, act=[%u,%u]",
           packet->device_id, packet->channel, (long)packet->trigger_value, packet->on_event, packet->route_mask,
           packet->static_action_id, packet->dynamic_action_id);
  return sys_power_monitor_add_callback(packet->device_id, packet->channel, packet->trigger_value,
                                        (sys_power_events_e)packet->on_event, packet->route_mask,
                                        packet->static_action_id, packet->dynamic_action_id);
}

#define HEADER_packet_sys_vreg_add_callback_t 0x3A
typedef struct __packed {
  uint8_t device_id;         //@required @alias Device ID
  uint8_t on_event;          //@required @alias Event @ref sys_power_events_e
  uint16_t route_mask;       //@required @alias Route Mask @note bitmask
  uint8_t static_action_id;  //@optional @alias Static Action ID @sentinel 0
  uint8_t dynamic_action_id; //@optional @alias Dynamic Action ID @sentinel 0
} packet_sys_vreg_add_callback_t;

static inline err_h decoder_packet_sys_vreg_add_callback_t(packet_sys_vreg_add_callback_t* packet) {
  ESP_LOGI(DEC_SYS_CONTRACTS_TAG, "adding vreg cb (dev %u): event=%u, route=0x%04X, act=[%u,%u]",
           packet->device_id, packet->on_event, packet->route_mask, packet->static_action_id, packet->dynamic_action_id);
  return sys_vreg_add_callback(packet->device_id, (sys_power_events_e)packet->on_event, packet->route_mask,
                               packet->static_action_id, packet->dynamic_action_id);
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
  X(HEADER_packet_sys_power_budget_update_source_t, packet_sys_power_budget_update_source_t, decoder_packet_sys_power_budget_update_source_t) \
  X(HEADER_packet_sys_vreg_set_enable_t, packet_sys_vreg_set_enable_t, decoder_packet_sys_vreg_set_enable_t)                                  \
  X(HEADER_packet_sys_vreg_set_voltage_t, packet_sys_vreg_set_voltage_t, decoder_packet_sys_vreg_set_voltage_t)                               \
  X(HEADER_packet_sys_vreg_set_current_t, packet_sys_vreg_set_current_t, decoder_packet_sys_vreg_set_current_t)                               \
  X(HEADER_packet_sys_power_monitor_get_voltage_t, packet_sys_power_monitor_get_voltage_t, decoder_packet_sys_power_monitor_get_voltage_t)    \
  X(HEADER_packet_sys_power_monitor_get_current_t, packet_sys_power_monitor_get_current_t, decoder_packet_sys_power_monitor_get_current_t)    \
  X(HEADER_packet_sys_power_usb_pd_set_t, packet_sys_power_usb_pd_set_t, decoder_packet_sys_power_usb_pd_set_t)                               \
  X(HEADER_packet_sys_power_usb_pd_list_t, packet_sys_power_usb_pd_list_t, decoder_packet_sys_power_usb_pd_list_t)                            \
  X(HEADER_packet_sys_power_usb_pd_get_limits_t, packet_sys_power_usb_pd_get_limits_t, decoder_packet_sys_power_usb_pd_get_limits_t)            \
  X(HEADER_packet_sys_power_monitor_add_callback_t, packet_sys_power_monitor_add_callback_t, decoder_packet_sys_power_monitor_add_callback_t) \
  X(HEADER_packet_sys_vreg_add_callback_t, packet_sys_vreg_add_callback_t, decoder_packet_sys_vreg_add_callback_t)                           \
  SYS_CONTRACTS_INSTALL_PACKET_LIST(X)

#define SYS_CONTRACTS_DECODE_CASE(header, packet_type, decoder_func)                    \
  case header: {                                                                        \
    packet_type packet;                                                                 \
    SE_RET_IF_ERR(convert_to_packet(data + 1, len - 1, &packet, sizeof(packet_type)));   \
    return decoder_func(&packet);                                                       \
  }

/**
 * @brief Class handler for SYS_CONTRACTS_CLASS_HEADER (0x01).
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
static inline err_h dec_sys_contracts_decode(const uint8_t* data, size_t len) {
  if (len == 0) {
    SE_RET_ERR(ERR_INTERFACE_SHORT_FRAME, .got = 0, .need = 1);
  }

  switch (data[0]) {
    SYS_CONTRACTS_PACKET_LIST(SYS_CONTRACTS_DECODE_CASE)
    default:
      ESP_LOGW(DEC_SYS_CONTRACTS_TAG, "unknown packet header 0x%02X", data[0]);
      SE_RET_ERR(ERR_INTERFACE_UNKNOWN_PACKET, .class_header = SYS_CONTRACTS_CLASS_HEADER, .packet_header = data[0]);
  }
}
