#pragma once
#include <stdint.h>
#include <sys/cdefs.h>
#include "sys_error.h"
#include "sys_ble.h"
#include "sys_device.h"
#include "sys_interface.h"
#include "sys_io.h"
#include "sys_power.h"

// ==========================================================================
// Device Management Packet decoders (0x10 - 0x19)
// ==========================================================================

#define HEADER_packet_sys_device_uninstall_t 0x10
typedef struct __packed {
  uint8_t device_id;
} packet_sys_device_uninstall_t;

static inline err_h decoder_packet_sys_device_uninstall_t(packet_sys_device_uninstall_t* packet) { return sys_device_uninstall(packet->device_id); }

#define HEADER_packet_sys_device_reset_t 0x11
typedef struct __packed {
  uint8_t device_id;
} packet_sys_device_reset_t;

static inline err_h decoder_packet_sys_device_reset_t(packet_sys_device_reset_t* packet) { return sys_device_reset(packet->device_id); }

#define HEADER_packet_sys_device_suspend_t 0x12
typedef struct __packed {
  uint8_t device_id;
} packet_sys_device_suspend_t;

static inline err_h decoder_packet_sys_device_suspend_t(packet_sys_device_suspend_t* packet) { return sys_device_suspend(packet->device_id); }

#define HEADER_packet_sys_device_resume_t 0x13
typedef struct __packed {
  uint8_t device_id;
} packet_sys_device_resume_t;

static inline err_h decoder_packet_sys_device_resume_t(packet_sys_device_resume_t* packet) { return sys_device_resume(packet->device_id); }

#define HEADER_packet_sys_device_suspend_all_t 0x14
typedef struct __packed {
  uint8_t dummy;
} packet_sys_device_suspend_all_t;

static inline err_h decoder_packet_sys_device_suspend_all_t(packet_sys_device_suspend_all_t* packet) { return sys_device_suspend_all(); }

#define HEADER_packet_sys_device_resume_all_t 0x15
typedef struct __packed {
  uint8_t dummy;
} packet_sys_device_resume_all_t;

static inline err_h decoder_packet_sys_device_resume_all_t(packet_sys_device_resume_all_t* packet) { return sys_device_resume_all(); }

#define HEADER_packet_sys_device_freeze_t 0x16
typedef struct __packed {
  uint8_t device_id;
} packet_sys_device_freeze_t;

static inline err_h decoder_packet_sys_device_freeze_t(packet_sys_device_freeze_t* packet) { return sys_device_freeze(packet->device_id); }

#define HEADER_packet_sys_device_sync_t 0x17
typedef struct __packed {
  uint8_t device_id;
} packet_sys_device_sync_t;

static inline err_h decoder_packet_sys_device_sync_t(packet_sys_device_sync_t* packet) { return sys_device_sync(packet->device_id); }

#define HEADER_packet_sys_device_freeze_all_t 0x18
typedef struct __packed {
  uint8_t dummy;
} packet_sys_device_freeze_all_t;

static inline err_h decoder_packet_sys_device_freeze_all_t(packet_sys_device_freeze_all_t* packet) { return sys_device_freeze_all(); }

#define HEADER_packet_sys_device_sync_all_t 0x19
typedef struct __packed {
  uint8_t dummy;
} packet_sys_device_sync_all_t;

static inline err_h decoder_packet_sys_device_sync_all_t(packet_sys_device_sync_all_t* packet) { return sys_device_sync_all(); }

// ==========================================================================
// IO Control Packet decoders (0x20 - 0x2F)
// ==========================================================================

#define HEADER_packet_sys_io_reset_t 0x20
typedef struct __packed {
  uint8_t device_id;
  uint8_t pin;
} packet_sys_io_reset_t;

static inline err_h decoder_packet_sys_io_reset_t(packet_sys_io_reset_t* packet) { return sys_io_reset(packet->device_id, packet->pin); }

#define HEADER_packet_sys_io_set_mode_t 0x21
typedef struct __packed {
  uint8_t device_id;
  uint8_t pin;
  uint8_t mode;  // sys_io_mode_e
} packet_sys_io_set_mode_t;

static inline err_h decoder_packet_sys_io_set_mode_t(packet_sys_io_set_mode_t* packet) { return sys_io_set_mode(packet->device_id, packet->pin, (sys_io_mode_e)packet->mode); }

#define HEADER_packet_sys_io_set_level_t 0x22
typedef struct __packed {
  uint8_t device_id;
  uint8_t pin;
  uint8_t level;
} packet_sys_io_set_level_t;

static inline err_h decoder_packet_sys_io_set_level_t(packet_sys_io_set_level_t* packet) { return sys_io_set_level(packet->device_id, packet->pin, packet->level != 0); }

#define HEADER_packet_sys_io_get_level_t 0x23
typedef struct __packed {
  uint8_t device_id;
  uint8_t pin;
} packet_sys_io_get_level_t;

static inline err_h decoder_packet_sys_io_get_level_t(packet_sys_io_get_level_t* packet) {
  bool level = false;
  err_h err = sys_io_get_level(packet->device_id, packet->pin, &level);
  if ((err == NULL)) {
    ESP_LOGI(__FILE_NAME__, "sys_io_get_level (dev_id: %u, pin: %u): level=%d", packet->device_id, packet->pin, level);
  }
  return err;
}

#define HEADER_packet_sys_io_toggle_t 0x24
typedef struct __packed {
  uint8_t device_id;
  uint8_t pin;
} packet_sys_io_toggle_t;

static inline err_h decoder_packet_sys_io_toggle_t(packet_sys_io_toggle_t* packet) { return sys_io_toggle(packet->device_id, packet->pin); }

#define HEADER_packet_sys_io_get_voltage_t 0x25
typedef struct __packed {
  uint8_t device_id;
  uint8_t pin;
} packet_sys_io_get_voltage_t;

static inline err_h decoder_packet_sys_io_get_voltage_t(packet_sys_io_get_voltage_t* packet) {
  uint32_t out_mV = 0;
  err_h err = sys_io_get_voltage(packet->device_id, packet->pin, &out_mV);
  if ((err == NULL)) {
    ESP_LOGI(__FILE_NAME__, "sys_io_get_voltage (dev_id: %u, pin: %u): mV=%lu", packet->device_id, packet->pin, (unsigned long)out_mV);
  }
  return err;
}

#define HEADER_packet_sys_io_set_voltage_t 0x26
typedef struct __packed {
  uint8_t device_id;
  uint8_t pin;
  uint32_t voltage_mV;
} packet_sys_io_set_voltage_t;

static inline err_h decoder_packet_sys_io_set_voltage_t(packet_sys_io_set_voltage_t* packet) { return sys_io_set_voltage(packet->device_id, packet->pin, packet->voltage_mV); }

#define HEADER_packet_sys_io_set_pwm_frequency_t 0x27
typedef struct __packed {
  uint8_t device_id;
  uint8_t pin;
  uint32_t frequency_HZ;
} packet_sys_io_set_pwm_frequency_t;

static inline err_h decoder_packet_sys_io_set_pwm_frequency_t(packet_sys_io_set_pwm_frequency_t* packet) { return sys_io_set_pwm_frequency(packet->device_id, packet->pin, packet->frequency_HZ); }

#define HEADER_packet_sys_io_set_pwm_duty_t 0x28
typedef struct __packed {
  uint8_t device_id;
  uint8_t pin;
  uint32_t duty;
} packet_sys_io_set_pwm_duty_t;

static inline err_h decoder_packet_sys_io_set_pwm_duty_t(packet_sys_io_set_pwm_duty_t* packet) { return sys_io_set_pwm_duty(packet->device_id, packet->pin, packet->duty); }

// ==========================================================================
// Power Control Packet decoders (0x30 - 0x3F)
// ==========================================================================

#define HEADER_packet_sys_power_budget_update_source_t 0x30
typedef struct __packed {
  uint32_t max_mV;
  uint32_t max_mA;
} packet_sys_power_budget_update_source_t;

static inline err_h decoder_packet_sys_power_budget_update_source_t(packet_sys_power_budget_update_source_t* packet) { return sys_power_budget_update_source(packet->max_mV, packet->max_mA); }

#define HEADER_packet_sys_vreg_set_enable_t 0x31
typedef struct __packed {
  uint8_t device_id;
  uint8_t state;
} packet_sys_vreg_set_enable_t;

static inline err_h decoder_packet_sys_vreg_set_enable_t(packet_sys_vreg_set_enable_t* packet) { return sys_vreg_set_enable(packet->device_id, packet->state != 0); }

#define HEADER_packet_sys_vreg_set_voltage_t 0x32
typedef struct __packed {
  uint8_t device_id;
  uint32_t voltage_mV;
} packet_sys_vreg_set_voltage_t;

static inline err_h decoder_packet_sys_vreg_set_voltage_t(packet_sys_vreg_set_voltage_t* packet) { return sys_vreg_set_voltage(packet->device_id, packet->voltage_mV); }

#define HEADER_packet_sys_vreg_set_current_t 0x33
typedef struct __packed {
  uint8_t device_id;
  uint32_t current_mA;
} packet_sys_vreg_set_current_t;

static inline err_h decoder_packet_sys_vreg_set_current_t(packet_sys_vreg_set_current_t* packet) { return sys_vreg_set_current(packet->device_id, packet->current_mA); }

#define HEADER_packet_sys_power_monitor_get_voltage_t 0x34
typedef struct __packed {
  uint8_t device_id;
  uint8_t channel;
} packet_sys_power_monitor_get_voltage_t;

static inline err_h decoder_packet_sys_power_monitor_get_voltage_t(packet_sys_power_monitor_get_voltage_t* packet) {
  int32_t out_mV = 0;
  err_h err = sys_power_monitor_get_voltage(packet->device_id, packet->channel, &out_mV);
  if ((err == NULL)) {
    ESP_LOGI(__FILE_NAME__, "sys_power_monitor_get_voltage (dev_id: %u, ch: %u): mV=%ld", packet->device_id, packet->channel, (long)out_mV);
  }
  return err;
}

#define HEADER_packet_sys_power_monitor_get_current_t 0x35
typedef struct __packed {
  uint8_t device_id;
  uint8_t channel;
} packet_sys_power_monitor_get_current_t;

static inline err_h decoder_packet_sys_power_monitor_get_current_t(packet_sys_power_monitor_get_current_t* packet) {
  int32_t out_mA = 0;
  err_h err = sys_power_monitor_get_current(packet->device_id, packet->channel, &out_mA);
  if ((err == NULL)) {
    ESP_LOGI(__FILE_NAME__, "sys_power_monitor_get_current (dev_id: %u, ch: %u): mA=%ld", packet->device_id, packet->channel, (long)out_mA);
  }
  return err;
}

#define HEADER_packet_sys_power_usb_pd_set_t 0x36
typedef struct __packed {
  uint8_t device_id;
  uint32_t voltage_mV;
  uint32_t current_mA;
} packet_sys_power_usb_pd_set_t;

static inline err_h decoder_packet_sys_power_usb_pd_set_t(packet_sys_power_usb_pd_set_t* packet) { return sys_power_usb_pd_set(packet->device_id, packet->voltage_mV, packet->current_mA); }

#define HEADER_packet_sys_power_usb_pd_list_t 0x37
typedef struct __packed {
  uint8_t device_id;
} packet_sys_power_usb_pd_list_t;

static inline err_h decoder_packet_sys_power_usb_pd_list_t(packet_sys_power_usb_pd_list_t* packet) { return sys_power_usb_pd_list(packet->device_id); }

#define HEADER_packet_sys_power_usb_pd_get_limits_t 0x38
typedef struct __packed {
  uint8_t device_id;
} packet_sys_power_usb_pd_get_limits_t;

static inline err_h decoder_packet_sys_power_usb_pd_get_limits_t(packet_sys_power_usb_pd_get_limits_t* packet) {
  uint32_t out_mV = 0;
  uint32_t out_mA = 0;
  err_h err = sys_power_usb_pd_get_limits(packet->device_id, &out_mV, &out_mA);
  if ((err == NULL)) {
    ESP_LOGI(__FILE_NAME__, "sys_power_usb_pd_get_limits (dev_id: %u): mV=%lu, mA=%lu", packet->device_id, (unsigned long)out_mV, (unsigned long)out_mA);
  }
  return err;
}

// Define the central list of packets for switch-case expansion in sys_interface.c
#define SYS_PACKET_LIST(X)                                                                                                                    \
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
  X(HEADER_packet_sys_io_reset_t, packet_sys_io_reset_t, decoder_packet_sys_io_reset_t)                                                       \
  X(HEADER_packet_sys_io_set_mode_t, packet_sys_io_set_mode_t, decoder_packet_sys_io_set_mode_t)                                              \
  X(HEADER_packet_sys_io_set_level_t, packet_sys_io_set_level_t, decoder_packet_sys_io_set_level_t)                                           \
  X(HEADER_packet_sys_io_get_level_t, packet_sys_io_get_level_t, decoder_packet_sys_io_get_level_t)                                           \
  X(HEADER_packet_sys_io_toggle_t, packet_sys_io_toggle_t, decoder_packet_sys_io_toggle_t)                                                    \
  X(HEADER_packet_sys_io_get_voltage_t, packet_sys_io_get_voltage_t, decoder_packet_sys_io_get_voltage_t)                                     \
  X(HEADER_packet_sys_io_set_voltage_t, packet_sys_io_set_voltage_t, decoder_packet_sys_io_set_voltage_t)                                     \
  X(HEADER_packet_sys_io_set_pwm_frequency_t, packet_sys_io_set_pwm_frequency_t, decoder_packet_sys_io_set_pwm_frequency_t)                   \
  X(HEADER_packet_sys_io_set_pwm_duty_t, packet_sys_io_set_pwm_duty_t, decoder_packet_sys_io_set_pwm_duty_t)                                  \
  X(HEADER_packet_sys_power_budget_update_source_t, packet_sys_power_budget_update_source_t, decoder_packet_sys_power_budget_update_source_t) \
  X(HEADER_packet_sys_vreg_set_enable_t, packet_sys_vreg_set_enable_t, decoder_packet_sys_vreg_set_enable_t)                                  \
  X(HEADER_packet_sys_vreg_set_voltage_t, packet_sys_vreg_set_voltage_t, decoder_packet_sys_vreg_set_voltage_t)                               \
  X(HEADER_packet_sys_vreg_set_current_t, packet_sys_vreg_set_current_t, decoder_packet_sys_vreg_set_current_t)                               \
  X(HEADER_packet_sys_power_monitor_get_voltage_t, packet_sys_power_monitor_get_voltage_t, decoder_packet_sys_power_monitor_get_voltage_t)    \
  X(HEADER_packet_sys_power_monitor_get_current_t, packet_sys_power_monitor_get_current_t, decoder_packet_sys_power_monitor_get_current_t)    \
  X(HEADER_packet_sys_power_usb_pd_set_t, packet_sys_power_usb_pd_set_t, decoder_packet_sys_power_usb_pd_set_t)                               \
  X(HEADER_packet_sys_power_usb_pd_list_t, packet_sys_power_usb_pd_list_t, decoder_packet_sys_power_usb_pd_list_t)                            \
  X(HEADER_packet_sys_power_usb_pd_get_limits_t, packet_sys_power_usb_pd_get_limits_t, decoder_packet_sys_power_usb_pd_get_limits_t)
