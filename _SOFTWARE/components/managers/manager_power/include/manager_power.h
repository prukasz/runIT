#pragma once 
#include "status.h"

typedef struct {
    void (*handler)(void *ctx); // The function to call
    void *ctx;                  // The specific handle/struct to pass to it
} pwr_event_cb_t;

typedef struct {

    void* reg_driver_handle_0; // Handle to the regulator driver (e.g., tps55289_handle_t)
    void* reg_driver_handle_1; // Handle to the regulator driver (e.g., tps55289_handle_t)
    void* power_monitor_handle; // Handle to the power monitoring driver (e.g., ina3221_handle_t)
    //
    pwr_event_cb_t reg0_ovp;
    pwr_event_cb_t reg0_ocp;
    pwr_event_cb_t reg0_scp;
    
    //
    pwr_event_cb_t reg1_ovp;
    pwr_event_cb_t reg1_ocp;
    pwr_event_cb_t reg1_scp;

    //
    pwr_event_cb_t power_warning;
    pwr_event_cb_t power_critical;

} manager_pwr_config_t;

status_rep_t manager_pwr_init(manager_pwr_config_t *config);