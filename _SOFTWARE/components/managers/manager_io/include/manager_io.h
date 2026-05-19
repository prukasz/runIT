#pragma once 
#include "status.h"

typedef struct {
    void (*handler)(void *ctx); // The function to call
    void *ctx;                  // The specific handle/struct to pass to it
} io_event_cb_t;

typedef struct{
    void* pwm_expander_dev_handle;
    void* adc_dev_handle;
    void* gpio_expander_dev_handle;
    io_event_cb_t adc_callback;
}manager_io_config_t;

status_rep_t manager_io_start(void* config);

