#pragma once

#include "sys_device.h"
#include "sys_io.h"
#include "sys_touch_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SYS_DEVICE_CONTRACT_TOUCH: 0x0Bxx, where 0x0B is the touch contract and xx its op. The touch panel of a display, one
 * point (XPT2046, FT6206 / FT6236 / FT6336, CST816S / CST816T / CST820, GT911), read by the LVGL adapter (SYS_TOUCH.MD).
 * CREATE      - touch_cfg_t, the same for all touch panels
 * POINT       - the point: pressed, x, y in the display's pixels, orientation 0
 * CALIBRATION - the raw range that maps to the display's pixels; resistive panels only
 * No events.
 */

/* touch_cfg_t.axes: how the touch panel's axes lie on the glass. */
//#ref-defines touch_axes @alias Touch axes @prefix TOUCH_AXES_
#define TOUCH_AXES_SWAP_XY  (1u << 0)  //@alias Swap X and Y @description The touch panel's X is the glass's Y
#define TOUCH_AXES_MIRROR_X (1u << 1)  //@alias Mirror X @description X counts from the right edge
#define TOUCH_AXES_MIRROR_Y (1u << 2)  //@alias Mirror Y @description Y counts from the bottom edge

/** @brief CREATE - a device contract - unified for all touch panels
 * A pin that is not used is SYS_IO_PIN_NONE. irq is a pin of the ESP32-S3 GPIO device, else ERR_DEV_BAD_PIN; cs and
 * reset may be on any device.
 */
typedef struct __attribute__((packed)) {
  uint8_t bus;              /**< I2C bus (sys_i2c) or SPI bus (sys_spi), the one the chip is on */
  uint8_t i2c_addr;         /**< 7-bit address, I2C chips; GT911: 0x5D or 0x14, set at its reset */
  uint32_t freq_hz;         /**< Bus clock for this chip */
  sys_io_pin_t cs;          /**< Chip select, SPI chips */
  sys_io_pin_t irq;         /**< Interrupt line; optional except where the chip needs it (GT911: address at reset) */
  sys_io_pin_t reset;       /**< Chip reset; optional except where the chip needs it (GT911) */
  uint16_t max_x;           /**< POINT's range: the display's native width (orientation 0) */
  uint16_t max_y;           /**< The display's native height (orientation 0) */
  uint8_t axes;             /**< TOUCH_AXES_* */
  uint8_t chip;             /**< The chip of the driver's family (the driver's own list) */
  int16_t cal_x_min;        /**< Resistive: raw X at the left edge (touch_calibration_t) */
  int16_t cal_x_max;        /**< Resistive: raw X at the right edge */
  int16_t cal_y_min;        /**< Resistive: raw Y at the top edge */
  int16_t cal_y_max;        /**< Resistive: raw Y at the bottom edge */
  uint16_t pressure_min;    /**< Resistive: below it the finger is up */
} touch_cfg_t;

/** @brief What POINT answers */
typedef struct __attribute__((packed)) {
  uint8_t pressed;  /**< 1 while touched */
  uint16_t x;       /**< 0 .. max_x - 1, the display's orientation 0; the last point while not pressed */
  uint16_t y;       /**< 0 .. max_y - 1 */
} touch_point_t;

/** @brief CALIBRATION args and answer: the raw values at the display's edges, orientation 0 */
typedef struct __attribute__((packed)) {
  int16_t x_min;  /**< Raw X at the left edge */
  int16_t x_max;  /**< Raw X at the right edge */
  int16_t y_min;  /**< Raw Y at the top edge */
  int16_t y_max;  /**< Raw Y at the bottom edge */
} touch_calibration_t;

/**
 * @brief The contract - X(P, NAME, op number, key_t, value_t, flags)
 * ops: POINT R, CALIBRATION RW (resistive panels; the others answer ERR_DEV_UNKNOWN_OP and leave it out of OPS)
 */
#define TOUCH_OPERATIONS(X, P)                                            \
  X(P, POINT,       0x00, sys_device_args_none_t, touch_point_t,       0) \
  X(P, CALIBRATION, 0x01, sys_device_args_none_t, touch_calibration_t, 0)

SYS_DEVICE_CONTRACT_DECLARE(TOUCH, SYS_DEVICE_CONTRACT_TOUCH)



/***************************************************************
* C API
******************************************************************/

/**
 * @brief Reads the point (op POINT)
 * @param[in]  device Touch device id
 * @param[out] out    Pressed, x, y
 * @return ERR_DEV_BAD_ARG for a NULL out, or the error of the device read
 */
err_h sys_touch_get_point(uint16_t device, touch_point_t* out);

/**
 * @brief Sets the calibration (op CALIBRATION, write), until the next CREATE
 * @param[in] device Touch device id
 * @param[in] cal    The raw values at the edges; min and max may be swapped (an axis read the other way)
 * @return ERR_DEV_BAD_ARG for a NULL cal or an axis whose min equals its max, or the error of the device write
 */
err_h sys_touch_set_calibration(uint16_t device, const touch_calibration_t* cal);

/**
 * @brief Reads the calibration (op CALIBRATION, read)
 * @param[in]  device Touch device id
 * @param[out] out    The raw values at the edges
 * @return ERR_DEV_BAD_ARG for a NULL out, or the error of the device read
 */
err_h sys_touch_get_calibration(uint16_t device, touch_calibration_t* out);


#ifdef __cplusplus
}
#endif
