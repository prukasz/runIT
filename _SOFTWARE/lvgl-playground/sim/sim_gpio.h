/* The simulated GPIO: a device of contract IO (type 0x7F02, no config) standing for the ESP32-S3 GPIO device, with
 * the display module wired to it. MODE, LEVEL and PWM_DUTY writes on its pins; the wires below reach the module, the
 * other pins go nowhere. CS and DC are taken by the SPI panel IO (mock_spi.c), not through this device. */
#pragma once

#define SIM_DEVICE_TYPE_GPIO 0x7F02u  /* host only: not in sys_device_types.h */

#define SIM_GPIO_PIN_LCD_RESET 4u   /* the ILI9341's RESX (mock_ili9341_resx) */
#define SIM_GPIO_PIN_LCD_BL    5u   /* the backlight LED (mock_panel_set_brightness), PWM */
#define SIM_GPIO_PIN_LCD_CS    10u
#define SIM_GPIO_PIN_LCD_DC    11u
#define SIM_GPIO_PINS          48u
