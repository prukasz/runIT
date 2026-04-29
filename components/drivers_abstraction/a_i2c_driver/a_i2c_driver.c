#include "a_i2c_driver.h"

static i2c_master_bus_handle_t a_i2c_bus_0;
static i2c_master_bus_handle_t a_i2c_bus_1;
static i2c_master_bus_config_t a_i2c_bus_0_config;
static i2c_master_bus_config_t a_i2c_bus_1_config;

status_err_report_t a_i2c_init(gpio_num_t sda_pin_bus0, gpio_num_t scl_pin_bus0, gpio_num_t sda_pin_bus1, gpio_num_t scl_pin_bus1) {
    status_err_report_t err;
    a_i2c_bus_0_config = (i2c_master_bus_config_t){
        .i2c_port = 0,
        .sda_io_num = sda_pin_bus0,
        .scl_io_num = scl_pin_bus0,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 1, 
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = 0,
            .allow_pd = 1,
        }, 
    };

    a_i2c_bus_1_config = (i2c_master_bus_config_t){
        .i2c_port = 1,
        .sda_io_num = sda_pin_bus1,
        .scl_io_num = scl_pin_bus1,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 1, 
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = 0,
            .allow_pd = 1,
        }, 
    };
    esp_err_t esp_err;
    esp_err = i2c_new_master_bus(&a_i2c_bus_0_config, &a_i2c_bus_0);
    if(esp_err  == ESP_ERR_INVALID_ARG) {
        STA_ERR_RETURN_PUSH_LOG(STA_ERR_S_C(ERR_INVALID_ARG, OWN_a_i2c_init), "Invalid args");   
    }else if (esp_err == ESP_ERR_NO_MEM)
    {
        STA_ERR_RETURN_PUSH_LOG(STA_ERR_S_C(ERR_INVALID_ARG, OWN_a_i2c_init), "No mem");   
    }else if (esp_err== ESP_ERR_NOT_FOUND)
    {
        STA_ERR_RETURN_PUSH_LOG(STA_ERR_S_C(ERR_INVALID_ARG, OWN_a_i2c_init), "No controllers aviable"); 
    }

    esp_err = i2c_new_master_bus(&a_i2c_bus_1_config, &a_i2c_bus_1);
    if(esp_err  == ESP_ERR_INVALID_ARG) {
        STA_ERR_RETURN_PUSH_LOG(STA_ERR_S_C(ERR_INVALID_ARG, OWN_a_i2c_init), "Invalid args");   
    }else if (esp_err == ESP_ERR_NO_MEM)
    {
        STA_ERR_RETURN_PUSH_LOG(STA_ERR_S_C(ERR_INVALID_ARG, OWN_a_i2c_init), "No mem");   
    }else if (esp_err== ESP_ERR_NOT_FOUND)
    {
        STA_ERR_RETURN_PUSH_LOG(STA_ERR_S_C(ERR_INVALID_ARG, OWN_a_i2c_init), "No controllers aviable"); 
    }

    return STA_ERR_OK;
}


status_err_report_t a_i2c_transmit(a_i2c_data_t){
    i2c_master_dev_t dev = {
        
    };
    i2c_master_transmit(i2c_master_dev_handle_t);
}

