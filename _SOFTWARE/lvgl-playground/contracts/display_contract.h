#pragma once

#include "sys_device.h" //zależności systemowe 
#include "sys_event.h" //zależności systemowe 
#include "sys_io.h" //zależności systemowe 
//ustawienie
typedef enum display_color_format_e {
  DISPLAY_COLOR_RGB565 = 0, 
  DISPLAY_COLOR_MONO1 = 1,   
} display_color_format_e;

//ustawienie 
typedef enum display_rgb_order_e {  
  DISPLAY_RGB_ORDER_RGB = 0,
  DISPLAY_RGB_ORDER_BGR = 1, 
} display_rgb_order_e;

//ustawienia jako flagi 
#define DISPLAY_FLAG_DMA (1u << 0) //czy bufor utworzony jest z dma 
#define DISPLAY_ORIENTATION_SWAP_XY  (1u << 0)
#define DISPLAY_ORIENTATION_MIRROR_X (1u << 1)
#define DISPLAY_ORIENTATION_MIRROR_Y (1u << 2)

//gdzie i ile wstawić(wysłać na wyświetlacz) -- patrz flush
typedef struct __attribute__((packed)) {
  uint16_t x1;
  uint16_t y1;
  uint16_t x2;
  uint16_t y2;
} display_window_t;

//info o wyświetlaczu - dla lvgl 
typedef struct __attribute__((packed)) {
  uint16_t width;           //szerokość 
  uint16_t height;          //wysokość
  uint8_t color_format;      /**< display_color_format_e; RGB565 goes out high byte first: the connector swaps the bytes */
  uint16_t flags;            // DISPLAY_FLAG_
  uint8_t align_y;           /**< A FLUSH window starts and ends on a multiple of this many rows (SSD1306: 8; 1: any) */
} display_geometry_t;

//co trzeba narysować
typedef struct {
  display_window_t window; //ile i gdzie 
  const void* pixels; //już zaalokowane 
} display_flush_args_t;

typedef struct __attribute__((packed)) {
  uint16_t brightness_duty;  //trafi do IO_PWM_DUTY : 12 bit (0-4095)
} display_backlight_args_t;

/**OPERACJE przez device call */
//(P, GEOMETRY,  0x00, sys_device_args_none_t, display_geometry_t)          
//X(P, FLUSH,     0x01, sys_device_args_none_t, display_flush_args_t)
//X(P, BACKLIGHT, 0x02, sys_device_args_none_t, display_backlight_args_t)

//Można zaczać renderowanie - wysyłanie skończone -- tu tylko rejestracja eventu jako typu
#define DISPLAY_EVENTS(X, P) \
  X(P, FLUSH_DONE, 0x00, SYS_EVENT_LOCAL)
SYS_EVENTS_DECLARE(DISPLAY) 

//czy użwać spi czy i2x
typedef enum display_bus_kind_e {
  DISPLAY_BUS_SPI = 0,  
  DISPLAY_BUS_I2C = 1,  
} display_bus_kind_e;

//struct przekazywany przy CREATE 
typedef struct __attribute__((packed)) {
  uint8_t bus_kind;              /**< display_bus_kind_e */
  uint8_t bus_index;             /**< SPI host or I2C bus (sys_i2c) */
  uint32_t freq_hz;
  uint8_t spi_mode;              /**< 0..3 (CPOL, CPHA); SPI only */
  uint8_t i2c_addr;              /**< 7-bit; I2C only */
  sys_io_pin_t cs_pin;
  sys_io_pin_t dc_pin;           /**< Required on SPI */
  sys_io_pin_t reset_pin;
  sys_io_pin_t backlight_pin;
  uint8_t bl_active_level;
  uint16_t brightness_defalut;  /**< After create and after RESUME */
  uint16_t width;                /**< Native size of the glass, in orientation 0 */
  uint16_t height;
  uint16_t offset_x;             /**< Where the glass starts in the controller's RAM (240x240 on 240x320 RAM) */
  uint16_t offset_y;
  uint8_t rgb_order;             /**< display_rgb_order_e */
  uint8_t invert;
  uint8_t orientation;           /**< DISPLAY_ORIENTATION_*, set once at create; a panel that can't do it in hardware: ERR_DEV_BAD_ARG */
  uint8_t variant;               /**< One of the driver's init tables: the same controller on another glass */
} display_cfg_t;

//funcje do użytku wewnętrznego  wrappery na device call

//uzyskaj geometrię dla lvgl
MUST_USE err_h sys_display_get_geometry(uint16_t device, display_geometry_t* out);
//lvgl wysyła - tylko wewnętrzna funkcja
MUST_USE err_h sys_display_flush(uint16_t device, const display_window_t* window, const void* pixels);
//tu zwykły op czyli też z aplikacji może użytkownik ustawić 
MUST_USE err_h sys_display_set_backlight(uint16_t device, uint16_t duty);  // 12 bit (0-4095), wprost do IO_PWM_DUTY
