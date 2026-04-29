#pragma once 
#include <stdint.h>
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include "status.h"

typedef struct{
    struct{
        const uint8_t* data;
        size_t len;
    }tx;
    struct{
        uint8_t* data;
        size_t len;
    }rx;
    uint8_t address;
}a_i2c_data_t;


status_err_report_t a_i2c_init_bus(i2c_port_num_t port, i2c_master_bus_handle_t* bus);

status_err_report_t a_i2c_transmit(i2c_port_num_t port, a_i2c_data_t* data);

status_err_report_t a_i2c_transmit_receive(i2c_port_num_t port, a_i2c_data_t* data);

status_err_report_t a_i2c_check_present(i2c_port_num_t port, uint8_t address, bool* result);

status_err_report_t a_i2c_scan_all(i2c_port_num_t port, uint8_t* result);

status_err_report_t a_i2c_receive(i2c_port_num_t port, a_i2c_data_t* data);

status_err_report_t a_i2c_add_device(i2c_port_num_t port, uint8_t address, uint32_t speed_hz);

status_err_report_t a_i2c_remove_device(i2c_port_num_t port, uint8_t address);



