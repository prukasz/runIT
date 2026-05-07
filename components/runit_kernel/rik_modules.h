#pragma once
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <driver/gpio.h>


/********************************************************************* 
Each component is independent part of system 
Here are located all wrappers to init them in main function 
Each wrapper shall be self contained so can be disabled wihout much effort 
Event groups can be assigned along with bits 
******************************************************************** */



esp_err_t rik_start_ble(EventGroupHandle_t rik_events);
esp_err_t rik_start_i2c(EventGroupHandle_t i2c_rik_events_0, EventGroupHandle_t i2c_rik_events_1,
    gpio_num_t sda_gpio_0, gpio_num_t scl_gpio_0, gpio_num_t sda_gpio_1, gpio_num_t scl_gpio_1);


void rik_start_interface(EventGroupHandle_t events);
void rik_start_status();