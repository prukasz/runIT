#pragma once

#include "sys_device.h"
#include "sys_io.h"
#include "sys_display_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SYS_DEVICE_CONTRACT_DISPLAY: 0x0Axx, where 0x0A is the display contract and xx its op. A colour panel on SPI
 * (ST7789, ILI9341, ST7735, GC9A01, ST7796, ILI9488), drawn by the LVGL adapter.
 * CREATE      - display_cfg_t, the same for all displays
 * GEOMETRY    - the size in the current orientation and the pixel format
 * BACKLIGHT   - brightness of the screen, set and read
 * HEALTH      - whether the panel still answers and still runs (a module whose SDO is wired: SYS_DISPLAY_FLAG_READABLE)
 * ORIENTATION - turns the picture in the panel (its scan direction); GEOMETRY answers the new size. Local
 * FLUSH       - draws a window of pixels. Local, asynchronous: returns once the transfer is queued; the driver calls
 *               the FLUSH's `done` from the transfer-done interrupt
 */

/***************************************************************
* Callbacks
******************************************************************/

/**
 * @brief End of an accepted FLUSH: once, from the transfer-done ISR (give a semaphore, nothing else). Never for a
 * refused FLUSH, never after RESET returns. No error: nothing is known after a transfer was accepted.
 * @param ctx The FLUSH's done_ctx
 */
typedef void (*display_flush_done_f)(void* ctx);

 /***************************************************************
* Types
******************************************************************/

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

/* display_cfg_t.panel: the controller, whose init sequence the driver sends; a driver refuses one it does not know
   (ERR_DEV_RANGE) */
//#ref-enum @alias Display controller
typedef enum sys_display_panel_e {
  SYS_DISPLAY_PANEL_ILI9341 = 0,  //@alias ILI9341 @description 240x320, RGB565
  SYS_DISPLAY_PANEL_ST7789 = 1,   //@alias ST7789 @description 240x320 RAM (240x240, 135x240, 170x320 glass), RGB565
  SYS_DISPLAY_PANEL_ST7735 = 2,   //@alias ST7735 @description 132x162 RAM (128x160, 80x160 glass), RGB565
  SYS_DISPLAY_PANEL_GC9A01 = 3,   //@alias GC9A01 @description 240x240 round, RGB565
  SYS_DISPLAY_PANEL_ST7796 = 4,   //@alias ST7796 @description 320x480, RGB565
  SYS_DISPLAY_PANEL_ILI9488 = 5,  //@alias ILI9488 @description 320x480, RGB888 (18 bit on SPI)
} sys_display_panel_e;

/* What HEALTH answers */
//#ref-enum @alias Display health
typedef enum sys_display_health_e {
  SYS_DISPLAY_HEALTH_UNKNOWN = 0,    //@alias Unknown @description The module cannot be read (SDO not wired): not checked
  SYS_DISPLAY_HEALTH_OK = 1,         //@alias OK @description The panel answers and runs as the driver set it
  SYS_DISPLAY_HEALTH_NO_ANSWER = 2,  //@alias No answer @description Nothing answers: the cable or the module's power is off
  SYS_DISPLAY_HEALTH_RESTARTED = 3,  //@alias Restarted @description The panel answers but runs no more: it lost its power and needs a RESET
} sys_display_health_e;

/* display_cfg_t.flags: how the module differs from the controller's defaults; 0 is an RGB glass, not inverted, not
   mirrored, its backlight on when the pin is high */
//#ref-defines display_flags @alias Display flags @prefix SYS_DISPLAY_FLAG_
#define SYS_DISPLAY_FLAG_BGR           (1u << 0)  //@alias BGR @description The glass's subpixels are blue, green, red
#define SYS_DISPLAY_FLAG_INVERT        (1u << 1)  //@alias Invert @description Inverted colours (most IPS glass)
#define SYS_DISPLAY_FLAG_MIRROR_X      (1u << 2)  //@alias Mirror X @description The glass is mounted mirrored left to right
#define SYS_DISPLAY_FLAG_MIRROR_Y      (1u << 3)  //@alias Mirror Y @description The glass is mounted mirrored top to bottom
#define SYS_DISPLAY_FLAG_BL_ACTIVE_LOW (1u << 4)  //@alias Backlight active low @description The backlight is on when its pin is low
#define SYS_DISPLAY_FLAG_READABLE      (1u << 5)  //@alias Readable @description The module's SDO is wired to the bus's MISO: HEALTH reads the panel

#define SYS_DISPLAY_FLAGS_ALL 0x3Fu  // every SYS_DISPLAY_FLAG_* bit

#define SYS_DISPLAY_BRIGHTNESS_MAX 4095  // = IO_PWM_DUTY_MAX: 12 bits

/***************************************************************
* Argument structs (cmd in / out)
******************************************************************/


/** @brief CREATE - a device contract - same for all displays
 * A pin that is not used is SYS_IO_PIN_NONE. cs and dc are pins of the ESP32-S3 GPIO device, else ERR_DEV_BAD_PIN;
 * reset and backlight may be on any sys_device.
 */
typedef struct __attribute__((packed)) {
  uint8_t bus;                  /**< SPI bus (sys_spi) */
  uint8_t spi_mode;             /**< 0 .. 3 (CPOL, CPHA) */
  uint32_t freq_hz;             /**< SPI clock */
  sys_io_pin_t cs;              /**< Chip select; SYS_IO_PIN_NONE: tied low on the module */
  sys_io_pin_t dc;              /**< Data / command, required */
  sys_io_pin_t reset;           /**< Panel reset; SYS_IO_PIN_NONE: a software reset (SWRESET) */
  sys_io_pin_t backlight;       /**< Backlight PWM; SYS_IO_PIN_NONE: always on, BACKLIGHT only remembered */
  uint16_t brightness_default;  /**< Backlight duty after CREATE and RESUME, 0 .. SYS_DISPLAY_BRIGHTNESS_MAX */
  uint16_t width;               /**< Native width of the glass in pixels (orientation 0) */
  uint16_t height;              /**< Native height of the glass in pixels (orientation 0) */
  uint16_t offset_x;            /**< Where the glass starts in the controller's RAM, orientation 0 (240x240 on 320) */
  uint16_t offset_y;
  uint8_t panel;                /**< sys_display_panel_e: the controller */
  uint8_t orientation;          /**< sys_display_orientation_e at start */
  uint8_t flags;                /**< SYS_DISPLAY_FLAG_*: colour order, inversion, mirror, backlight level, readable */
} display_cfg_t;

/** @brief What GEOMETRY answers: the panel in its current orientation */
typedef struct __attribute__((packed)) {
  uint16_t width;      /**< Pixels */
  uint16_t height;     /**< Pixels */
  uint8_t format;      /**< sys_display_format_e: the bytes of a buffer, as the driver sends them */
  uint32_t flush_max;  /**< The largest FLUSH len in bytes the driver takes (its bus's transfer size) */
} display_geometry_t;

/** @brief A rectangle of the panel, both corners included */
typedef struct __attribute__((packed)) {
  uint16_t x1;  /**< Left column */
  uint16_t y1;  /**< Top row */
  uint16_t x2;  /**< Right column */
  uint16_t y2;  /**< Bottom row */
} display_window_t;

/** @brief BACKLIGHT args and answer: PWM duty of the backlight (the read: what it is now; 0 while suspended) */
typedef struct __attribute__((packed)) {
  uint16_t brightness_duty;  /**< 0 .. SYS_DISPLAY_BRIGHTNESS_MAX */
} display_backlight_args_t;

/** @brief What HEALTH answers */
typedef struct __attribute__((packed)) {
  uint8_t health;      /**< sys_display_health_e */
  uint8_t power_mode;  /**< What the panel answered to its power mode read (MIPI DCS 0x0A); 0 when not read */
} display_health_t;

/** @brief ORIENTATION args and answer */
typedef struct __attribute__((packed)) {
  uint8_t orientation;  /**< sys_display_orientation_e */
} display_orientation_args_t;


/** @brief FLUSH args, C only (the op is local): the pixels, the window they fill, the end callback */
typedef struct {
  const void* pixels;         /**< The window's pixels in the GEOMETRY format, row by row; DMA-capable memory */
  uint32_t len;               /**< Bytes: window width x height x bytes per pixel, at most GEOMETRY's flush_max */
  display_window_t window;    /**< Where the pixels go (inside the panel, see GEOMETRY) */
  display_flush_done_f done;  /**< The end of the transfer, from its interrupt; required */
  void* done_ctx;             /**< Passed to done */
} display_flush_args_t;

/***************************************************************
* All device OPs
******************************************************************/

/**
 * @brief The contract - X(P, NAME, op number, key_t, value_t, flags)
 * ops: GEOMETRY R, BACKLIGHT RW, FLUSH W, ORIENTATION RW, HEALTH R; FLUSH and ORIENTATION local (SYS_DEVICE_OP_LOCAL)
 */
#define DISPLAY_OPERATIONS(X, P)                                                                   \
  X(P, GEOMETRY,    0x00, sys_device_args_none_t, display_geometry_t,         0)                   \
  X(P, BACKLIGHT,   0x01, sys_device_args_none_t, display_backlight_args_t,   0)                   \
  X(P, FLUSH,       0x02, sys_device_args_none_t, display_flush_args_t,       SYS_DEVICE_OP_LOCAL) \
  X(P, ORIENTATION, 0x03, sys_device_args_none_t, display_orientation_args_t, SYS_DEVICE_OP_LOCAL) \
  X(P, HEALTH,      0x04, sys_device_args_none_t, display_health_t,           0)

SYS_DEVICE_CONTRACT_DECLARE(DISPLAY, SYS_DEVICE_CONTRACT_DISPLAY)



/***************************************************************
* C API
******************************************************************/

/**
 * @brief Reads the panel's geometry (op GEOMETRY)
 * @param[in]  device Display device id
 * @param[out] out    Width, height, pixel format and the largest FLUSH
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
 * @brief Reads the backlight (op BACKLIGHT, read): the duty now, 0 while the display is suspended
 * @param[in]  device Display device id
 * @param[out] out    PWM duty, 0 .. 4095
 * @return ERR_DEV_BAD_ARG for a NULL out, or the error of the device read
 */
err_h sys_display_get_backlight(uint16_t device, uint16_t* out);

/**
 * @brief Reads whether the panel still answers (op HEALTH), after the transfer in flight; UNKNOWN without READABLE.
 * @param[in]  device Display device id
 * @param[out] out    sys_display_health_e and the power mode read
 * @return ERR_DEV_BAD_ARG for a NULL out, or the error of the device read
 */
err_h sys_display_get_health(uint16_t device, display_health_t* out);

/**
 * @brief Turns the picture (op ORIENTATION, write; local); GEOMETRY then answers the new size. Refused while a FLUSH
 * is in flight (ERR_DISPLAY_FLUSH_BUSY).
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
 * @param[in] len      Bytes: window width x height x bytes per pixel, at most GEOMETRY's flush_max
 * @param[in] window   The window to draw
 * @param[in] done     The end of the transfer (ISR context)
 * @param[in] done_ctx Passed to done
 * @return ERR_DEV_BAD_ARG for a NULL window or done, or the driver's error passed (BAD_ARG, BAD_WINDOW, FLUSH_BUSY,
 * TRANSFER: the bus refused it; SUSPENDED...): then done is not called and nothing is sent
 */
err_h sys_display_flush(uint16_t device, const void* pixels, uint32_t len, const display_window_t* window,
                        display_flush_done_f done, void* done_ctx);


#ifdef __cplusplus
}
#endif
