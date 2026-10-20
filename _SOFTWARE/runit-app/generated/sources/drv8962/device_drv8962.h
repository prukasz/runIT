#pragma once

#include "sys_device.h"
#include "sys_hbridge.h"
#include "sys_io.h"
#include "sys_power_monitor.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DRV8962_CURRENT_LIMIT_MAX_MA 10000u

//#ref-enum @alias DRV8962 Topology
typedef enum drv8962_topology_e {
  DRV8962_TOPOLOGY_2_FULL_BRIDGES = 0,  //@alias Two full bridges @description Two full bridges: ch0 (OUT1+OUT2), ch1 (OUT3+OUT4)
  DRV8962_TOPOLOGY_4_HALF_BRIDGES = 1,  //@alias Four half bridges @description Four half bridges: ch0..ch3 (OUT1..OUT4)
} drv8962_topology_e;

/* The chip has no bus: every signal is a pin of another device. Indexes into drv8962_cfg_t.pin. IN1..IN4 carry the
   PWM, EN1..EN4 enable the half bridges, IPROPI1..IPROPI4 are the current sense outputs (ADC pins), nSLEEP wakes /
   resets the chip, nFAULT reports faults and VREF (a DAC pin) sets the chopping current. */
enum {
  DRV8962_PIN_IN1 = 0,
  DRV8962_PIN_EN1 = 4,
  DRV8962_PIN_IPROPI1 = 8,
  DRV8962_PIN_NSLEEP = 12,
  DRV8962_PIN_NFAULT = 13,
  DRV8962_PIN_VREF = 14,
  DRV8962_PINS = 15,
};

/**
 * @brief DRV8962 configuration struct (wire format for CREATE).
 */
typedef struct __attribute__((packed)) {
  uint8_t topology;               /**< Half-bridge topology (drv8962_topology_e) */
  sys_io_pin_t pin[DRV8962_PINS]; /**< Signals mapped to external device pins */
  uint32_t pwm_freq_Hz;           /**< PWM frequency (0 selects 20 kHz default) */
  uint32_t ripropi_ohms[4];       /**< Current-sense resistor for each half bridge */
  uint32_t current_limit_mA;      /**< The chip's current limit, every channel (one VREF); 0 = disabled */
  uint8_t turn_off_at_ocp;        /**< 1 = brake channel upon overcurrent, 0 = continue */
} drv8962_cfg_t;

/* Own ops */
#define DRV8962_OP_CURRENT_LIMIT SYS_DEVICE_OWN_OP(0x00)  // drv8962_limit_t: the chip's chopping limit (VREF), every channel
#define DRV8962_OP_CLEAR_FAULT   SYS_DEVICE_OWN_OP(0x01)  // hbridge_channel_args_t: nSLEEP reset pulse clears the chip's latches; the channel brakes
#define DRV8962_OP_FAULT         SYS_DEVICE_OWN_OP(0x02)  // hbridge_channel_args_t -> drv8962_fault_t
#define DRV8962_OP_SETTINGS      SYS_DEVICE_OWN_OP(0x03)  // drv8962_settings_t, both sides: the live settings (the read answers them)

typedef struct __attribute__((packed)) {
  uint32_t limit_mA;  /**< Current limit in mA, every channel (0 = no limit) */
} drv8962_limit_t;

typedef struct __attribute__((packed)) {
  uint8_t fault;   /**< 1 = fault present, 0 = no fault */
  uint8_t reason;  /**< Fault reason (sys_hbridge_fault_reason_e) */
} drv8962_fault_t;

/* The settings of drv8962_cfg_t that change live, with the same names. */
typedef struct __attribute__((packed)) {
  uint8_t topology;           /**< drv8962_topology_e */
  uint32_t current_limit_mA;  /**< The chip's current limit, every channel; 0 = disabled */
  uint8_t turn_off_at_ocp;    /**< 1 = brake a channel over the limit */
} drv8962_settings_t;

#ifdef __cplusplus
}
#endif
