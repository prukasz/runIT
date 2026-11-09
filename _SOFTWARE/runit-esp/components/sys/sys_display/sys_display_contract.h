#pragma once

#include "sys_device.h"
#include "sys_io.h"
#include "sys_display_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SYS_DEVICE_CONTRACT_DISPLAY: 0x0Axx, where 0x0A is the display contract and xx its op. A colour panel on SPI
 * (ST7789, ILI9341, ST7735, GC9A01, ST7796, ILI9488), drawn by the LVGL adapter (SYS_DISPLAY.MD).
 * CREATE      - display_cfg_t, the same for all displays
 * GEOMETRY    - what the LVGL adapter needs to create its buffers: the size in the current orientation, the format
 * BACKLIGHT   - brightness of the screen
 * ORIENTATION - turns the picture (the panel's scan direction, in hardware); GEOMETRY answers the new size. Local:
 *               only the screen adapter turns the panel (its ROTATION), so LVGL always draws for the panel as it is
 * FLUSH       - draws a window of the adapter's pixels; it carries pointers, so it is local (C only,
 *               SYS_DEVICE_OP_LOCAL); asynchronous (it returns once the driver has queued the pixels, and the driver
 *               calls the FLUSH's `done` from the transfer-done interrupt)
 * No events: the end of a FLUSH is its callback, not the event queue (SYS_DISPLAY.MD "The end of a FLUSH").
 */

//#ref-enum @alias Pixel format
typedef enum sys_display_format_e {
  SYS_DISPLAY_FORMAT_RGB565_SWAPPED = 0,  //@alias RGB565 @description 16 bit, 2 bytes per pixel, the high byte first (as the panel takes it)
  SYS_DISPLAY_FORMAT_RGB888 = 1,          //@alias RGB888 @description 24 bit, 3 bytes per pixel B, G, R; an 18-bit panel uses the 6 high bits
} sys_display_format_e;

//#ref-enum @alias Display orientation
typedef enum sys_display_orientation_e {
  SYS_DISPLAY_ORIENTATION_0 = 0,    //@alias 0 deg @description Native
  SYS_DISPLAY_ORIENTATION_90 = 1,   //@alias 90 deg @description Rotated 90 deg
  SYS_DISPLAY_ORIENTATION_180 = 2,  //@alias 180 deg @description Rotated 180 deg
  SYS_DISPLAY_ORIENTATION_270 = 3,  //@alias 270 deg @description Rotated 270 deg
} sys_display_orientation_e;

//#ref-enum @alias Colour order
typedef enum sys_display_rgb_order_e {
  SYS_DISPLAY_RGB_ORDER_RGB = 0,  //@alias RGB @description The glass's subpixels are red, green, blue
  SYS_DISPLAY_RGB_ORDER_BGR = 1,  //@alias BGR @description The glass's subpixels are blue, green, red
} sys_display_rgb_order_e;

#define SYS_DISPLAY_BRIGHTNESS_MAX 4095  // = IO_PWM_DUTY_MAX: 12 bits

/** @brief CREATE - a device contract - unified for all displays
 * A pin that is not used is SYS_IO_PIN_NONE. cs and dc are pins of the ESP32-S3 GPIO device (the SPI driver switches
 * them in every transaction), else ERR_DEV_BAD_PIN; reset and backlight may be on any device.
 */
typedef struct __attribute__((packed)) {
  uint8_t bus;                  /**< SPI bus (sys_spi) */
  uint8_t spi_mode;             /**< 0 .. 3 (CPOL, CPHA); a module without CS often needs 3 */
  uint32_t freq_hz;             /**< SPI clock */
  sys_io_pin_t cs;              /**< Chip select; SYS_IO_PIN_NONE: tied low on the module */
  sys_io_pin_t dc;              /**< Data / command, required */
  sys_io_pin_t reset;           /**< Panel reset */
  sys_io_pin_t backlight;       /**< Backlight PWM */
  uint8_t bl_active_level;      /**< 1: the backlight is on when the pin is high, 0: when it is low */
  uint16_t brightness_default;  /**< Backlight duty after CREATE and RESUME, 0 .. SYS_DISPLAY_BRIGHTNESS_MAX */
  uint16_t width;               /**< Native width of the glass in pixels (orientation 0) */
  uint16_t height;              /**< Native height of the glass in pixels (orientation 0) */
  uint16_t offset_x;            /**< Where the glass starts in the controller's RAM, orientation 0 (240x240 on 320) */
  uint16_t offset_y;
  uint8_t panel;                /**< The controller's init sequence (the driver's own list) */
  uint8_t rgb_order;            /**< sys_display_rgb_order_e */
  uint8_t invert;               /**< 1: inverted colours (most IPS glass) */
  uint8_t orientation;          /**< sys_display_orientation_e at start */
} display_cfg_t;

/** @brief What GEOMETRY answers: the panel in its current orientation */
typedef struct __attribute__((packed)) {
  uint16_t width;   /**< Pixels */
  uint16_t height;  /**< Pixels */
  uint8_t format;   /**< sys_display_format_e: the bytes of a buffer, as the driver sends them */
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

/** @brief ORIENTATION args and answer */
typedef struct __attribute__((packed)) {
  uint8_t orientation;  /**< sys_display_orientation_e */
} display_orientation_args_t;

/**
 * @brief The end of a FLUSH: called once by the driver after the transfer of an accepted FLUSH, also after a failed
 * one; never for a FLUSH it refused (that one returned an err_h). Runs in the transfer-done interrupt: it may only do
 * what an ISR may (give a semaphore FromISR), never take a lock or call a device. It carries no error: a transfer's
 * error comes back from FLUSH (SYS_DISPLAY.MD "Transfer errors").
 * @param ctx The FLUSH's done_ctx
 */
typedef void (*display_flush_done_f)(void* ctx);

/** @brief FLUSH args, C only (never a frame: the op is local): the pixels, the window they fill, the end callback */
typedef struct {
  const void* pixels;         /**< The window's pixels in the GEOMETRY format, row by row; DMA-capable memory */
  uint32_t len;               /**< Bytes: window width x height x bytes per pixel */
  display_window_t window;    /**< Where the pixels go (inside the panel, see GEOMETRY) */
  display_flush_done_f done;  /**< The end of the transfer, from its interrupt; required */
  void* done_ctx;             /**< Passed to done */
} display_flush_args_t;

/**
 * @brief The contract - X(P, NAME, op number, key_t, value_t, flags)
 * ops: GEOMETRY R, BACKLIGHT W, FLUSH W, ORIENTATION RW; FLUSH and ORIENTATION local (SYS_DEVICE_OP_LOCAL)
 */
#define DISPLAY_OPERATIONS(X, P)                                                                   \
  X(P, GEOMETRY,    0x00, sys_device_args_none_t, display_geometry_t,         0)                   \
  X(P, BACKLIGHT,   0x01, sys_device_args_none_t, display_backlight_args_t,   0)                   \
  X(P, FLUSH,       0x02, sys_device_args_none_t, display_flush_args_t,       SYS_DEVICE_OP_LOCAL) \
  X(P, ORIENTATION, 0x03, sys_device_args_none_t, display_orientation_args_t, SYS_DEVICE_OP_LOCAL)

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
 * @brief Turns the picture (op ORIENTATION, write; local: the screen adapter's ROTATION); GEOMETRY then answers the
 * new size. Refused while a FLUSH is in flight (ERR_DISPLAY_FLUSH_BUSY).
 * @param[in] device      Display device id
 * @param[in] orientation sys_display_orientation_e
 * @return ERR_DEV_RANGE for an orientation past 270 deg, or the error of the device write
 */
err_h sys_display_set_orientation(uint16_t device, uint8_t orientation);

/**
 * @brief Reads the orientation (op ORIENTATION, read)
 * @param[in]  device Display device id
 * @param[out] out    sys_display_orientation_e
 * @return ERR_DEV_BAD_ARG for a NULL out, or the error of the device read
 */
err_h sys_display_get_orientation(uint16_t device, uint8_t* out);

/**
 * @brief Draws a window of pixels on the panel (op FLUSH). It returns once the driver has queued the transfer; `done`
 * is called from its interrupt when it ends, and the pixels stay untouched until then.
 * @param[in] device   Display device id
 * @param[in] pixels   The window's pixels in the GEOMETRY format, in DMA-capable memory
 * @param[in] len      Bytes: window width x height x bytes per pixel
 * @param[in] window   The window to draw
 * @param[in] done     The end of the transfer (ISR context)
 * @param[in] done_ctx Passed to done
 * @return ERR_DEV_BAD_ARG for a NULL window or done, or the driver's error passed (BAD_ARG, BAD_WINDOW, FLUSH_BUSY,
 * TRANSFER: the bus refused it now, or the previous transfer failed after it was accepted; SUSPENDED...): then done is
 * not called and nothing is sent
 */
err_h sys_display_flush(uint16_t device, const void* pixels, uint32_t len, const display_window_t* window,
                        display_flush_done_f done, void* done_ctx);


#ifdef __cplusplus
}
#endif
