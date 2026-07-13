#pragma once

#include <esp_log.h>
#include "sys_i2c.h"

#define INA3221_I2C_ADDR_GND 0x40 // A0 to GND
#define INA3221_I2C_ADDR_VS  0x41 // A0 to Vs+
#define INA3221_I2C_ADDR_SDA 0x42 // A0 to SDA
#define INA3221_I2C_ADDR_SCL 0x43 // A0 to SCL

#define INA3221_BUS_NUMBER 3  // Number of channels

#define INA3221_DEFAULT_CONFIG                   (0x7127UL)
#define INA3221_DEFAULT_MASK                     (0x0002UL)
#define INA3221_DEFAULT_POWER_UPPER_LIMIT        (0x2710UL) //10V
#define INA3221_DEFAULT_POWER_LOWER_LIMIT        (0x2328UL) //9V

#define INA3221_MASK_CONFIG (0x7C00)

typedef enum
{
    INA3221_AVG_1 = 0,  // Default
    INA3221_AVG_4,
    INA3221_AVG_16,
    INA3221_AVG_64,
    INA3221_AVG_128,
    INA3221_AVG_256,
    INA3221_AVG_512,
    INA3221_AVG_1024,
} ina3221_avg_t;

typedef enum
{
    INA3221_CHANNEL_1 = 0,
    INA3221_CHANNEL_2,
    INA3221_CHANNEL_3,
    INA3221_CHANNEL_ALL = 3
} ina3221_channel_t;

typedef enum
{
    INA3221_CT_140 = 0,
    INA3221_CT_204,
    INA3221_CT_332,
    INA3221_CT_588,
    INA3221_CT_1100,  ///< Default
    INA3221_CT_2116,
    INA3221_CT_4156,
    INA3221_CT_8244,
} ina3221_ct_t;

typedef union
{
    struct
    {
        uint16_t esht : 1; ///< Enable/Disable shunt measure    // LSB
        uint16_t ebus : 1; ///< Enable/Disable bus measure
        uint16_t mode : 1; ///< Single shot measure or continuous mode
        uint16_t vsht : 3; ///< Shunt voltage conversion time
        uint16_t vbus : 3; ///< Bus voltage conversion time
        uint16_t avg  : 3; ///< number of sample collected and averaged together
        uint16_t ch3  : 1; ///< Enable/Disable channel 3
        uint16_t ch2  : 1; ///< Enable/Disable channel 2
        uint16_t ch1  : 1; ///< Enable/Disable channel 1
        uint16_t rst  : 1; ///< Set this bit to 1 to reset device  // MSB
    };
    uint16_t config_register;
} ina3221_config_t;

typedef union
{
    struct
    {
        uint16_t cvrf : 1; // Conversion ready flag (1: ready)   // LSB
        uint16_t tcf  : 1; // Timing control flag
        uint16_t pvf  : 1; // Power valid flag
        uint16_t wf   : 3; // Warning alert flag (Read mask to clear)
        uint16_t sf   : 1; // Sum alert flag (Read mask to clear)
        uint16_t cf   : 3; // Critical alert flag (Read mask to clear)
        uint16_t cen  : 1; // Critical alert latch (1:enable)
        uint16_t wen  : 1; // Warning alert latch (1:enable)
        uint16_t scc3 : 1; // channel 3 sum (1:enable)
        uint16_t scc2 : 1; // channel 2 sum (1:enable)
        uint16_t scc1 : 1; // channel 1 sum (1:enable)
        uint16_t      : 1; // Reserved         //MSB
    };
    uint16_t mask_register;
} ina3221_mask_t;

typedef struct
{
    sys_i2c_driver_header_t header;
    uint16_t shunt_val_cfg[INA3221_BUS_NUMBER];     // Memory of shunt value (mOhm)
    ina3221_config_t config;                        // Memory of config
    ina3221_mask_t mask;                            // Memory of mask_config
} _ina3221_data_t;

typedef _ina3221_data_t* ina3221_handle_t;

ina3221_handle_t ina3221_new(uint8_t i2c_address, bool i2c_bus_num);
void ina3221_delete(ina3221_handle_t handle);
esp_err_t ina3221_start(ina3221_handle_t handle);
esp_err_t ina3221_get_status(ina3221_handle_t handle);
esp_err_t ina3221_set_options(ina3221_handle_t handle, bool bus, bool mode, bool shunt_val_cfg);
esp_err_t ina3221_enable_channel(ina3221_handle_t handle, bool ch1, bool ch2, bool ch3);
void ina3221_set_shunt_resistor(ina3221_handle_t handle, uint16_t resistance, ina3221_channel_t channel);
esp_err_t ina3221_enable_channel_sum(ina3221_handle_t handle, bool ch1, bool ch2, bool ch3);
esp_err_t ina3221_enable_latch_pin(ina3221_handle_t handle, bool warning, bool critical);
esp_err_t ina3221_set_average(ina3221_handle_t handle, ina3221_avg_t avg);
esp_err_t ina3221_set_bus_conversion_time(ina3221_handle_t handle, ina3221_ct_t ct);
esp_err_t ina3221_set_shunt_conversion_time(ina3221_handle_t handle, ina3221_ct_t ct);
esp_err_t ina3221_reset(ina3221_handle_t handle);
esp_err_t ina3221_read_bus_voltage(ina3221_handle_t handle, uint8_t channel, float* out_mv);
esp_err_t ina3221_read_shunt_current(ina3221_handle_t handle, uint8_t channel, float* out_ma);
esp_err_t ina3221_read_sum_shunt_voltage(ina3221_handle_t handle, float* out_mv);
esp_err_t ina3221_set_alert(ina3221_handle_t handle, ina3221_channel_t channel, int32_t current_mA, bool is_critical);
esp_err_t ina3221_set_sum_warning_alert(ina3221_handle_t handle, uint32_t voltage_mv);