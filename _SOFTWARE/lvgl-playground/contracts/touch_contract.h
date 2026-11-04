#pragma once

#include "sys_device.h"
#include "sys_event.h"
#include "sys_io.h"



#define TOUCH_ORIENTATION_SWAP_XY  (1u << 0)  //ustawienia dla sterownika
#define TOUCH_ORIENTATION_MIRROR_X (1u << 1)  //ustawienia dla sterownika
#define TOUCH_ORIENTATION_MIRROR_Y (1u << 2)  //ustawienia dla sterownika


//to dostaje lvgl 
typedef struct __attribute__((packed)) {
  uint8_t pressed; //czy naciśnięte - czy stare dane 
  uint16_t x;
  uint16_t y;
} touch_point_t;

//rezystancyjny tylko 
typedef struct __attribute__((packed)) {
  int16_t x_min;
  int16_t x_max;
  int16_t y_min;
  int16_t y_max;
} touch_calibration_args_t;


//operacje udostępnione 
//X(P, POINT,       0x00, sys_device_args_none_t, touch_point_t)          
//X(P, CALIBRATION, 0x01, sys_device_args_none_t, touch_calibration_args_t)

SYS_DEVICE_CONTRACT_DECLARE(TOUCH, SYS_DEVICE_CONTRACT_TOUCH)

//typ interfejsu
typedef enum touch_bus_kind_e {
  TOUCH_BUS_I2C = 0, 
  TOUCH_BUS_SPI = 1, 
} touch_bus_kind_e;

//ustawienie podczas CREATE
typedef struct __attribute__((packed)) {
  uint8_t bus_kind;                /**< touch_bus_kind_e */
  uint8_t bus_index;               /**< I2C bus index (sys_i2c) or SPI host */
  uint32_t freq_hz;
  uint8_t i2c_addr;                
  sys_io_pin_t cs_pin;             /**< SPI only */
  sys_io_pin_t irq_pin;            /**< Optional: with it POINT skips the bus read while nobody touches */
  sys_io_pin_t reset_pin;
  uint16_t max_x;                  /**< The size POINT reports in: the panel's width in orientation 0 */
  uint16_t max_y;                  /**< The panel's height */
  uint8_t orientation;             /**< TOUCH_ORIENTATION_*, set once; the same value as the panel's */
  touch_calibration_args_t cal;    /**< Resistive only; the others ignore it */
  uint16_t pressure_min;           /**< Resistive only: below it the finger is up */
} touch_cfg_t;


//do użytku wewnętrznego - wrappery na device call
//dla lvgl
MUST_USE err_h sys_touch_get_point(uint16_t device, touch_point_t* out);
//ustawia app / użytkownik 
MUST_USE err_h sys_touch_set_calibration(uint16_t device, const touch_calibration_args_t* cal);
