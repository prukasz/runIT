/* Host stand-in for ESP-IDF's esp_lcd_panel_io.h: the part of the SPI panel IO a display driver uses, with the same
 * names and signatures, so the driver (driver/lcd_spi.c) builds unchanged on the board. Behind it is mock_spi.c: the
 * bytes go to the simulated controller (mock_ili9341.c) instead of a wire.
 *
 * As esp_lcd does it on SPI: tx_param and tx_color first wait until the colour transfer in flight has ended; tx_param
 * then sends the command and its parameters and returns; tx_color sends the command, queues the colour bytes and
 * returns; on_color_trans_done is called when they have gone (on the board from the SPI interrupt, here from the bus's
 * transfer thread). esp_lcd_panel_io_del drops a transfer that never ends (no callback after it returns). */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

typedef struct esp_lcd_panel_io_t *esp_lcd_panel_io_handle_t;
typedef int esp_lcd_spi_bus_handle_t;  /* the SPI host: (esp_lcd_spi_bus_handle_t)SPI2_HOST on the board */

typedef struct {
    int unused;
} esp_lcd_panel_io_event_data_t;

/* Returns whether a higher-priority task was woken (ISR context on the board). */
typedef bool (*esp_lcd_panel_io_color_trans_done_cb_t)(esp_lcd_panel_io_handle_t panel_io,
                                                       esp_lcd_panel_io_event_data_t *edata, void *user_ctx);

typedef struct {
    int cs_gpio_num;          /* -1: no CS */
    int dc_gpio_num;
    int spi_mode;             /* 0 .. 3 */
    unsigned int pclk_hz;
    size_t trans_queue_depth;
    esp_lcd_panel_io_color_trans_done_cb_t on_color_trans_done;
    void *user_ctx;
    int lcd_cmd_bits;
    int lcd_param_bits;
} esp_lcd_panel_io_spi_config_t;

esp_err_t esp_lcd_new_panel_io_spi(esp_lcd_spi_bus_handle_t bus, const esp_lcd_panel_io_spi_config_t *io_config,
                                   esp_lcd_panel_io_handle_t *ret_io);
esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t io, int lcd_cmd, const void *param, size_t param_size);
/* Sends the command, then reads param_size bytes (the bus's MISO; 0xFF from its pull-up when nothing drives it) */
esp_err_t esp_lcd_panel_io_rx_param(esp_lcd_panel_io_handle_t io, int lcd_cmd, void *param, size_t param_size);
esp_err_t esp_lcd_panel_io_tx_color(esp_lcd_panel_io_handle_t io, int lcd_cmd, const void *color, size_t color_size);
esp_err_t esp_lcd_panel_io_del(esp_lcd_panel_io_handle_t io);
