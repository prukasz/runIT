#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include <esp_log.h>

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

/**
 * Number of samples
 */
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

/**
 * Channel selection list
 */
typedef enum
{
    INA3221_CHANNEL_1 = 0,
    INA3221_CHANNEL_2,
    INA3221_CHANNEL_3,
    INA3221_CHANNEL_ALL = 3
} ina3221_channel_t;

/**
 * Conversion time in us
 */
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

/**
 * Config description register
 */
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

/**
 * Mask/enable description register
 */
typedef union
{
    struct
    {
        uint16_t cvrf : 1; // Conversion ready flag (1: ready)   // LSB
        uint16_t tcf  : 1; // Timing control flag
        uint16_t pvf  : 1; // Power valid flag
        uint16_t wf   : 3; // Warning alert flag (Read mask to clear) (order : Channel1:channel2:channel3)
        uint16_t sf   : 1; // Sum alert flag (Read mask to clear)
        uint16_t cf   : 3; // Critical alert flag (Read mask to clear) (order : Channel1:channel2:channel3)
        uint16_t cen  : 1; // Critical alert latch (1:enable)
        uint16_t wen  : 1; // Warning alert latch (1:enable)
        uint16_t scc3 : 1; // channel 3 sum (1:enable)
        uint16_t scc2 : 1; // channel 2 sum (1:enable)
        uint16_t scc1 : 1; // channel 1 sum (1:enable)
        uint16_t      : 1; // Reserved         //MSB
    };
    uint16_t mask_register;
} ina3221_mask_t;

/**
 * @brief Main struct for INA3221 driver, containing all necessary information and state
 */
typedef struct
{
    /* Driver task handle for alerts and deferred operations */
    TaskHandle_t driver_task_handle;
    /* I2C device configuration filled up automatically*/
    i2c_device_config_t i2c_device_config;
    /* I2C master device handle, needs to be initialized */
    i2c_master_dev_handle_t i2c_master_dev_handle;

    uint16_t shunt_val_cfg[INA3221_BUS_NUMBER];     // Memory of shunt value (mOhm)
    ina3221_config_t config;                        // Memory of ina3221 config
    ina3221_mask_t mask; // Memory of mask_config
    
    /**
     * @param last_readings readings data stored, and updated by dereffered reading operations in task.
     */
    struct
    {
        float bus_voltage[INA3221_BUS_NUMBER];       // mV
        float shunt_voltage[INA3221_BUS_NUMBER];     // mV
        float shunt_current[INA3221_BUS_NUMBER];     // mA
        float sum_shunt_voltage;                     // mV
    } last_readings;

    /**
     * @brief User-defined callbacks for handling alerts
     * Index mapping: (channel * 2 + (is_critical ? 1 : 0))
     * - Index 0,2,4: Warning for CH1, CH2, CH3
     * - Index 1,3,5: Critical for CH1, CH2, CH3
     */
    void (*user_callback[6])(void *arg);

    void *user_callback_arg[6];

    
    /*Flag for critical pin triggered*/
    bool alert_critical;
    /*Flag for warning pin triggered*/
    bool alert_warning;

    /**
     * @brief Flags for dereffered operation in task
     * This allows to make non blocking reading calls
     */
    struct{
        uint8_t read_bus_voltage          :1; 
        uint8_t read_bus_voltage_periodic :1; 
        uint8_t read_current              :1;
        uint8_t read_current_periodic     :1;
        uint8_t read_current_sum          :1;
        uint8_t read_current_sum_periodic :1;
        uint8_t read_status               :1;
        uint8_t read_status_periodic      :1;
    }to_update;
    
} _ina3221_data_t;

typedef _ina3221_data_t* ina3221_handle_t;

/**
 * @brief Create a new instance of INA3221 driver
 * @param i2c_address I2C address of the INA3221 device (use INA3221_I2C_ADDR_GND, INA3221_I2C_ADDR_VS, INA3221_I2C_ADDR_SDA, or INA3221_I2C_ADDR_SCL)
 * @warning I2C master_master_dev_handle inside the struct is not initialized please add to selected bus
 * @return Handle to the INA3221 device, or NULL if allocation failed
 */
ina3221_handle_t ina3221_new(uint8_t i2c_address);

/**
 * @brief Read status register from device, immediate or deferred (non-blocking) update
 * @param handle Device descriptor
 * @param immediate Immediate or deferred update
 * @return ESP_OK to indicate success
 */
esp_err_t ina3221_get_status(ina3221_handle_t handle, bool immediate);

/**
 * @brief Set options for bus and shunt, update immidiately
 * @param handle Device descriptor
 * @param bus 1(enable) or 0(disable) bus voltage measurement
 * @param mode 1(continuous) or 0(single shot) measurement mode
 * @param shunt_val_cfg 1(enable) or 0(disable) shunt voltage measurement
 * @return ESP_OK to indicate success
 */
esp_err_t ina3221_set_options(ina3221_handle_t handle, bool bus, bool mode, bool shunt_val_cfg);

/**
 * @brief Select channels to be enabled for measurement, update immidiately
 * @param handle Device descriptor
 * @param ch1 1 (enable) or 0(disable) 
 * @param ch2 1 (enable) or 0(disable)
 * @param ch3 1 (enable) or 0(disable)
 * @return ESP_OK to indicate success
 */
esp_err_t ina3221_enable_channel(ina3221_handle_t handle, bool ch1, bool ch2, bool ch3);

/**
 * @brief Select channel to be sum (don't impact enable channel status), update immidiately
 * @param handle Device descriptor
 * @param ch1 1 (enable) or 0(disable)
 * @param ch2 1 (enable) or 0(disable)
 * @param ch3 1 (enable) or 0(disable)
 * @return ESP_OK to indicate success
 */
esp_err_t ina3221_enable_channel_sum(ina3221_handle_t handle, bool ch1, bool ch2, bool ch3);

/**
 * @brief Select value of shunt resistor on the channel, update immidiately
 * @param handle Device descriptor
 * @param resistance Resistance of the shunt resistor in mOhm
 * @param channel selected channel, if INA3221_CHANNEL_ALL is selected, all channels will be set with the same resistance value
 */
void ina3221_set_shunt_resistor(ina3221_handle_t handle, uint16_t resistance, ina3221_channel_t channel);

/**
 * @brief enable/disable latch on warning and critical alert pin, update immidiately
 * @param handle Device descriptor
 * @param warning 1(Latch) or 0(Live) on warning alert pin
 * @param critical 1(Latch) or 0(Live) on critical alert pin
 * @return ESP_OK to indicate success
 */
esp_err_t ina3221_enable_latch_pin(ina3221_handle_t handle, bool warning, bool critical);

/**
 * @brief Set average (number of samples measured), update immidiately
 * @note Reduce noise by averaging samples, decrease effective sampling rate, default is 1 sample (no average)
 * @param handle Device descriptor
 * @param avg Value of average selection
 * @return ESP_OK to indicate success
 */
esp_err_t ina3221_set_average(ina3221_handle_t handle, ina3221_avg_t avg);

/**
 * @brief Set conversion time for bus, update immidiately
 * @note Longer conversion time can help to reduce noise, default is 1100us for bus and 588us for shunt
 * @param handle Device descriptor
 * @param ct Value of conversion time selection
 * @return ESP_OK to indicate success
 */
esp_err_t ina3221_set_bus_conversion_time(ina3221_handle_t handle, ina3221_ct_t ct);

/**
 * @brief Set conversion time for shunt, update immidiately
 * @note Longer conversion time can help to reduce noise, default is 1100us for bus and 588us for shunt
 * @param handle Device descriptor
 * @param ct Value of conversion time selection
 * @return ESP_OK to indicate success
 */
esp_err_t ina3221_set_shunt_conversion_time(ina3221_handle_t handle, ina3221_ct_t ct);

/**
 * @brief Reset device, update immidiately
 * Device will be configured like POR (Power-On-Reset)
 * @param handle Device descriptor
 * @return ESP_OK to indicate success
 */
esp_err_t ina3221_reset(ina3221_handle_t handle);

/**
 * @brief Update bus voltage readings in mV, immediate or deferred (non-blocking) update
 * @param handle Device descriptor
 * @param immediate Immediate or deferred update
 * @return ESP_OK to indicate success
 */
esp_err_t ina3221_update_buses_readings(ina3221_handle_t handle, bool immediate);

/**
 * @brief Update shunt voltage and current readings, immediate or deferred (non-blocking) update
 * @param handle Device descriptor
 * @param immediate Immediate or deferred update
 * @return ESP_OK to indicate success
 */
esp_err_t ina3221_update_shunts_readings(ina3221_handle_t handle, bool immediate);

/**
 * @brief Get Shunt-voltage (mV) sum value of selected channels, immediate or deferred (non-blocking) update
 * @note Please eneable sum for selected channels using ina3221_enable_channel_sum() before using this function, otherwise the value will be 0
 * @param handle Device descriptor
 * @param immediate Immediate or deferred update
 * @return ESP_OK to indicate success
 */
esp_err_t ina3221_get_sum_shunt_value(ina3221_handle_t handle, bool immediate);

/**
 * @brief Set Critical alert, update immidiately
 * @nosubgrouping Alert when average measurement(s) is greater than the set value
 * @param handle Device descriptor
 * @param channel Select channel value to set
 * @param current Value to set (mA). Negative values are allowed for reverse current thresholds.
 * @return ESP_OK to indicate success
 */
esp_err_t ina3221_set_critical_alert(ina3221_handle_t handle, ina3221_channel_t channel, int32_t current_mA);

/**
 * @brief Set Warning alert, update immidiately
 * Alert when average measurement(s) is greater
 * @param handle Device descriptor
 * @param channel Select channel value to set
 * @param current Value to set (mA). Negative values are allowed for reverse current thresholds.
 * @return ESP_OK to indicate success
 */
esp_err_t ina3221_set_warning_alert(ina3221_handle_t handle, ina3221_channel_t channel, int32_t current_mA);

/**
 * @brief Set Sum Warning alert, update immidiately
 * @note Compared to each completed cycle of all selected channels : Sum register
 * @param handle Device descriptor
 * @param voltage voltage to set (mV) //  max : 655.32
 * @return ESP_OK to indicate success
 */
esp_err_t ina3221_set_sum_warning_alert(ina3221_handle_t handle, uint32_t voltage_mv);


/**
 * @brief Reset alert hardware settings and flags
 * Disables warning and critical alert latches, reads mask register to clear alert flags,
 * and resets internal alert flags
 * @param handle INA3221 handle
 * @return ESP_OK to indicate success
 */
esp_err_t ina3221_reset_alerts(ina3221_handle_t handle);

/**
 * @brief Configure periodic readings for INA3221
 * @param handle INA3221 handle
 * @param bus_voltage Whether to read bus voltage periodically
 * @param current Whether to read current periodically
 * @param current_sum Whether to read sum current periodically
 * @param status Whether to read status periodically
 */
void ina3221_cfg_periodic_reading(ina3221_handle_t handle, bool bus_voltage, bool current, bool current_sum, bool status);

/**
 * @brief Register user callback for alert events
 * @param handle INA3221 handle
 * @param callback User callback function to be called when alert is triggered, can be NULL to just clear the callback
 * @param arg Argument to be passed to the user callback function
 * @param channel Target channel (0-2 for CH1, CH2, CH3)
 * @param is_critical Whether the callback is for critical alerts (true) or warning alerts (false)
 * @note Callback invoked after driver task receives alert;
 */
void ina3221_register_user_callback(ina3221_handle_t handle, void (*callback)(void *), void *arg, uint8_t channel, bool is_critical);


/**
 * @breif GPIO isr callback 
 * @param arg INA3221 handle
 * @note This function is called in ISR context, it will set alert flag and notify driver
 */
void p_current_monitor_intr_pin_crit_callback(void *arg);

/**
 * @breif GPIO isr callback 
 * @param arg INA3221 handle
 * @note This function is called in ISR context, it will set warning flag and notify driver
 */
void ina3221_isr_callback_warning(void *arg);