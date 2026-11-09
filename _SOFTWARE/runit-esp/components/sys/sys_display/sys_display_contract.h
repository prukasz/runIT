#pragma once

#include "sys_device.h"
#include "sys_event.h"
#include "sys_io.h"
#include "sys_display_errors.h"

#ifdef __cplusplus
extern "C" {
#endif
  
/*
 * SYS_DEVICE_CONTRACT_DISPLAY: 0x0Axx, where 0x0A is the display contract and xx its op.
 * CREATE    - display_cfg_t, the same for all displays
 * GEOMETRY  - what the LVGL adapter needs to create its buffers (screen size)
 * BACKLIGHT - brightness of the screen
 * FLUSH     - draws a window of a buffer; asynchronous (the driver returns once it has accepted the pixels)
 * Event FLUSH_DONE: the transfer ended; value = 0 or the error code of the transfer.
 */

//#ref-enum @alias Pixel format
typedef enum sys_display_format_e {
  SYS_DISPLAY_FORMAT_RGB565 = 0,  //@alias RGB565 @description 16 bit, 2 bytes per pixel
  SYS_DISPLAY_FORMAT_MONO = 1,    //@alias Mono @description 1 bit per pixel (OLED pages)
} sys_display_format_e;

//#ref-enum @alias Display orientation
typedef enum sys_display_orientation_e {
  SYS_DISPLAY_ORIENTATION_0 = 0,    //@alias 0 deg @description Native
  SYS_DISPLAY_ORIENTATION_90 = 1,   //@alias 90 deg @description Rotated 90 deg
  SYS_DISPLAY_ORIENTATION_180 = 2,  //@alias 180 deg @description Rotated 180 deg
  SYS_DISPLAY_ORIENTATION_270 = 3,  //@alias 270 deg @description Rotated 270 deg
} sys_display_orientation_e;

#define SYS_DISPLAY_BRIGHTNESS_MAX 4095  // = IO_PWM_DUTY_MAX: 12 bits

/** @brief CREATE - a device contract - unified for all displays
*A pin that is not used is SYS_IO_PIN_NONE
*/
typedef struct __attribute__((packed)) {
  uint8_t bus;             /**< Bus index (SPI or I2C, as the driver needs) */
  uint8_t address;         /**< I2C address, unused on SPI */
  sys_io_pin_t cs;         /**< Chip select (SPI) */
  sys_io_pin_t dc;         /**< Data / command (SPI) */
  sys_io_pin_t reset;      /**< Panel reset */
  sys_io_pin_t backlight;  /**< Backlight PWM */
  uint16_t width;          /**< Native width of the panel in pixels (orientation 0) */
  uint16_t height;         /**< Native height of the panel in pixels (orientation 0) */
  uint8_t panel;           /**< Panel variant of the driver (its own list) */
  uint8_t orientation;     /**< sys_display_orientation_e */
} display_cfg_t;

/** @brief What GEOMETRY answers: the panel in its current orientation */
typedef struct __attribute__((packed)) {
  uint16_t width;   /**< Pixels */
  uint16_t height;  /**< Pixels */
  uint8_t format;   /**< sys_display_format_e */
} display_geometry_t;

/** @brief A rectangle of the panel, both corners included */
typedef struct __attribute__((packed)) {
  uint16_t x1;  /**< Left column */
  uint16_t y1;  /**< Top row */
  uint16_t x2;  /**< Right column */
  uint16_t y2;  /**< Bottom row */
} display_window_t;

/** @brief BACKLIGHT args: PWM duty of the backlight. */
typedef struct __attribute__((packed)) {
  uint16_t brightness_duty;  /**< 0 .. SYS_DISPLAY_BRIGHTNESS_MAX */
} display_backlight_args_t;

/** @brief FLUSH args: the id of the buffer (no pointer in a frame) and the window to draw */
typedef struct __attribute__((packed)) {
  uint8_t buffer_id;        /**< Buffer id, below SYS_DISPLAY_BUFFERS */
  display_window_t window;  /**< Where the pixels go (inside the panel, see GEOMETRY) */
} display_flush_args_t;

/**
 * @brief The contract - X(P, NAME, op number, key_t, value_t)
 * ops: GEOMETRY R, BACKLIGHT W, FLUSH W
 */
#define DISPLAY_OPERATIONS(X, P)                                            \
  X(P, GEOMETRY,  0x00, sys_device_args_none_t, display_geometry_t)         \
  X(P, BACKLIGHT, 0x01, sys_device_args_none_t, display_backlight_args_t)   \
  X(P, FLUSH,     0x02, sys_device_args_none_t, display_flush_args_t)

SYS_DEVICE_CONTRACT_DECLARE(DISPLAY, SYS_DEVICE_CONTRACT_DISPLAY)



/*************************************************************** 
* C API
******************************************************************/

/**
 * @brief Reads the panel's geometry (op GEOMETRY)
 * @param[in]  device Display device id
 * @param[out] out    Width, height and pixel format
 * @return ERR_DEV_BAD_ARG for a NULL out, or the error of the device read
 */
err_h sys_display_get_geometry(uint16_t device, display_geometry_t* out);

/**
 * @brief Sets the backlight (op BACKLIGHT)
 * @param[in] device          Display device id
 * @param[in] brightness_duty PWM duty, 0 .. 4095
 * @return ERR_DEV_RANGE for a duty above 4095, or the error of the device write
 */
err_h sys_display_set_backlight(uint16_t device, uint16_t brightness_duty);

/**
 * @brief Draws a window of a buffer on the panel (op FLUSH); FLUSH_DONE is published when the transfer ends
 * @param[in] device Display device id
 * @param[in] buffer Buffer id set with sys_display_buffer_set
 * @param[in] window The window to draw
 * @return ERR_DEV_BAD_ARG for a NULL window, or the driver's error passed (BAD_BUFFER, BAD_WINDOW, FLUSH_BUSY, SUSPENDED...)
 */
err_h sys_display_flush(uint16_t device, uint8_t buffer, const display_window_t* window);



/*************************************************************** 
* Buffers setters and getters - lvgl adapter and driver bridge
******************************************************************/

#define SYS_DISPLAY_BUFFERS 2u  // buffer ids 0 .. 1: double buffering

/**
 * @brief The adapter sets its own draw buffer at the given id
 * @param[in] buffer_id     Buffer id (0 .. SYS_DISPLAY_BUFFERS - 1)
 * @param[in] pixels The buffer's pixels; NULL clears the entry (size becomes 0)
 * @param[in] size   The buffer's size in bytes
 * @return ERR_DISPLAY_BAD_BUFFER for an id past SYS_DISPLAY_BUFFERS
 */
MUST_USE err_h sys_display_buffer_set(uint8_t buffer_id, const void* pixels, uint32_t size);

/**
 * @brief The driver gets the buffer named by a FLUSH (the id given to sys_display_flush)
 * @param[in]  buffer_id         Buffer id (0 .. SYS_DISPLAY_BUFFERS - 1)
 * @param[out] out_pixels The buffer's pixels
 * @param[out] out_size   The buffer's size in bytes
 * @return ERR_DEV_BAD_ARG for a NULL output, ERR_DISPLAY_BAD_BUFFER for an id past SYS_DISPLAY_BUFFERS or a buffer not
 * set
 */
MUST_USE err_h sys_display_buffer_get(uint8_t buffer_id, const void** out_pixels, uint32_t* out_size);

/**
 * @brief The events of the contract - X(P, NAME, event number, flags)
 * FLUSH_DONE is published from driver, after every accepted FLUSH sys_display_flush -> NULL
 * value = 0, or the error code of the transfer (the .code of ERR_DISPLAY_TRANSFER): the adapter builds the error from it.
 * Flags 0: for the adapter, not for the app
 */
#define DISPLAY_EVENTS(X, P) \
  X(P, FLUSH_DONE, 0x00, 0)
SYS_EVENTS_DECLARE(DISPLAY)


#ifdef __cplusplus
}
#endif
