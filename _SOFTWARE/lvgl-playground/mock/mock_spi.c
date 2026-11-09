#include "mock_spi.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_lcd_panel_io.h"
#include "mock_ili9341.h"
#include "mock_panel.h"

typedef struct esp_lcd_panel_io_t {
    esp_lcd_panel_io_spi_config_t cfg;
    pthread_mutex_t m;
    pthread_cond_t c;
    pthread_t thread;
    /* the colour transfer in flight */
    bool inflight;
    bool started;           /* the thread took it */
    bool hung;              /* it never ends */
    bool drop;              /* esp_lcd_panel_io_del gave it up */
    bool quit;
    const uint8_t *color;
    size_t len;
    mock_spi_fault_e fault; /* taken at tx_color */
} esp_lcd_panel_io_t;

static volatile mock_spi_fault_e s_fault;
static volatile bool s_trace;

void mock_spi_set_fault(mock_spi_fault_e fault)
{
    s_fault = fault;
}

void mock_spi_set_trace(bool on)
{
    s_trace = on;
}

static void sleep_us(uint64_t us)
{
    struct timespec ts = {.tv_sec = (time_t)(us / 1000000u), .tv_nsec = (long)(us % 1000000u) * 1000L};
    nanosleep(&ts, NULL);
}

static uint64_t wire_us(const esp_lcd_panel_io_t *io, size_t bytes)
{
    const unsigned hz = io->cfg.pclk_hz != 0u ? io->cfg.pclk_hz : 1000000u;
    return (uint64_t)bytes * 8u * 1000000u / hz;
}

/* The colour transfer: as long as the bytes take on the clock, then into the controller, then the callback. */
static void *transfer_thread(void *arg)
{
    esp_lcd_panel_io_t *io = arg;
    pthread_mutex_lock(&io->m);
    while (!io->quit) {
        if (!io->inflight || io->started) {
            pthread_cond_wait(&io->c, &io->m);
            continue;
        }
        io->started = true;
        const uint8_t *color = io->color;
        const size_t len = io->len;
        const mock_spi_fault_e fault = io->fault;
        pthread_mutex_unlock(&io->m);

        sleep_us(wire_us(io, len));
        if (fault == MOCK_SPI_FAULT_HANG) {
            pthread_mutex_lock(&io->m);
            io->hung = true;
            while (!io->drop && !io->quit) {
                pthread_cond_wait(&io->c, &io->m);
            }
            io->inflight = io->started = io->hung = io->drop = false;
            pthread_cond_broadcast(&io->c);
            continue;
        }
        if (fault == MOCK_SPI_FAULT_NOISE) {
            uint8_t *bad = malloc(len);
            if (bad != NULL) {
                memcpy(bad, color, len);
                for (size_t i = (size_t)rand() % 7u; i < len; i += 1u + (size_t)rand() % 13u) {
                    bad[i] ^= (uint8_t)(1u << (rand() & 7));
                }
                mock_ili9341_data(bad, len);
                free(bad);
            }
        } else {
            mock_ili9341_data(color, len);
        }
        mock_panel_account((uint32_t)len);
        if (io->cfg.on_color_trans_done != NULL) {
            esp_lcd_panel_io_event_data_t ev = {0};
            io->cfg.on_color_trans_done(io, &ev, io->cfg.user_ctx);
        }
        pthread_mutex_lock(&io->m);
        io->inflight = io->started = false;
        pthread_cond_broadcast(&io->c);
    }
    pthread_mutex_unlock(&io->m);
    return NULL;
}

esp_err_t esp_lcd_new_panel_io_spi(esp_lcd_spi_bus_handle_t bus, const esp_lcd_panel_io_spi_config_t *io_config,
                                   esp_lcd_panel_io_handle_t *ret_io)
{
    (void)bus;
    if (io_config == NULL || ret_io == NULL || io_config->dc_gpio_num < 0 || io_config->lcd_cmd_bits != 8 ||
        io_config->lcd_param_bits != 8) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_lcd_panel_io_t *io = calloc(1, sizeof(*io));
    if (io == NULL) return ESP_ERR_NO_MEM;
    io->cfg = *io_config;
    pthread_mutex_init(&io->m, NULL);
    pthread_cond_init(&io->c, NULL);
    if (pthread_create(&io->thread, NULL, transfer_thread, io) != 0) {
        pthread_cond_destroy(&io->c);
        pthread_mutex_destroy(&io->m);
        free(io);
        return ESP_ERR_NO_MEM;
    }
    if (s_trace) printf("spi: panel IO up, %u Hz, mode %d, CS %d, DC %d\n", io_config->pclk_hz, io_config->spi_mode,
                        io_config->cs_gpio_num, io_config->dc_gpio_num);
    *ret_io = io;
    return ESP_OK;
}

/* As esp_lcd: a polling transaction waits for the queued colour transfer first (forever, if it hangs). */
static void wait_idle(esp_lcd_panel_io_t *io)
{
    pthread_mutex_lock(&io->m);
    while (io->inflight) {
        pthread_cond_wait(&io->c, &io->m);
    }
    pthread_mutex_unlock(&io->m);
}

static void trace_cmd(int cmd, const uint8_t *p, size_t n)
{
    if (!s_trace) return;
    printf("spi: %02X", (unsigned)cmd);
    for (size_t i = 0; i < n && i < 16; i++) printf(" %02X", p[i]);
    printf(n > 16 ? " ...\n" : "\n");
}

esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t io, int lcd_cmd, const void *param, size_t param_size)
{
    if (io == NULL || (param == NULL && param_size != 0u)) return ESP_ERR_INVALID_ARG;
    wait_idle(io);
    trace_cmd(lcd_cmd, param, param_size);
    sleep_us(wire_us(io, 1u + param_size));
    mock_ili9341_command((uint8_t)lcd_cmd);
    if (param_size != 0u) mock_ili9341_data(param, param_size);
    return ESP_OK;
}

esp_err_t esp_lcd_panel_io_rx_param(esp_lcd_panel_io_handle_t io, int lcd_cmd, void *param, size_t param_size)
{
    if (io == NULL || param == NULL || param_size == 0u) return ESP_ERR_INVALID_ARG;
    wait_idle(io);
    sleep_us(wire_us(io, 1u + param_size));
    mock_ili9341_command((uint8_t)lcd_cmd);
    mock_ili9341_read(param, param_size);
    if (s_trace) {
        printf("spi: %02X read", (unsigned)lcd_cmd);
        for (size_t i = 0; i < param_size; i++) printf(" %02X", ((const uint8_t *)param)[i]);
        printf("\n");
    }
    return ESP_OK;
}

esp_err_t esp_lcd_panel_io_tx_color(esp_lcd_panel_io_handle_t io, int lcd_cmd, const void *color, size_t color_size)
{
    if (io == NULL || color == NULL || color_size == 0u) return ESP_ERR_INVALID_ARG;
    wait_idle(io);
    const mock_spi_fault_e fault = s_fault;
    if (fault == MOCK_SPI_FAULT_HANG) {
        s_fault = MOCK_SPI_FAULT_NONE;  /* one transfer hangs: the driver's RESET after it finds the bus working */
    }
    if (fault == MOCK_SPI_FAULT_REFUSE) {
        if (s_trace) printf("spi: %02X refused\n", (unsigned)lcd_cmd);
        return ESP_ERR_INVALID_STATE;
    }
    if (s_trace) printf("spi: %02X [%u B of pixels]\n", (unsigned)lcd_cmd, (unsigned)color_size);
    mock_ili9341_command((uint8_t)lcd_cmd);
    pthread_mutex_lock(&io->m);
    io->color = color;
    io->len = color_size;
    io->fault = fault;
    io->started = false;
    io->inflight = true;
    pthread_cond_broadcast(&io->c);
    pthread_mutex_unlock(&io->m);
    return ESP_OK;
}

esp_err_t esp_lcd_panel_io_del(esp_lcd_panel_io_handle_t io)
{
    if (io == NULL) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&io->m);
    if (io->hung || (io->inflight && io->fault == MOCK_SPI_FAULT_HANG)) {
        io->drop = true;
        pthread_cond_broadcast(&io->c);
    }
    while (io->inflight) {
        pthread_cond_wait(&io->c, &io->m);
    }
    io->quit = true;
    pthread_cond_broadcast(&io->c);
    pthread_mutex_unlock(&io->m);
    pthread_join(io->thread, NULL);
    pthread_cond_destroy(&io->c);
    pthread_mutex_destroy(&io->m);
    if (s_trace) printf("spi: panel IO deleted\n");
    free(io);
    return ESP_OK;
}
