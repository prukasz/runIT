#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <driver/i2c_master.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define PCA9685_I2C_DEFAULT_FREQUENCY   400000 //1MHz not working right now
#define PCA9685_MAX_PWM_VALUE           4095   
#define PCA9685_TIMEOUT_MS              100
#define PCA9685_CHANNEL_ALL             16

typedef enum {
    PCA9685_CHANNEL_0 = 0,
    PCA9685_CHANNEL_1,
    PCA9685_CHANNEL_2,
    PCA9685_CHANNEL_3,
    PCA9685_CHANNEL_4,
    PCA9685_CHANNEL_5,
    PCA9685_CHANNEL_6,
    PCA9685_CHANNEL_7,
    PCA9685_CHANNEL_8,
    PCA9685_CHANNEL_9,
    PCA9685_CHANNEL_10,
    PCA9685_CHANNEL_11,
    PCA9685_CHANNEL_12,
    PCA9685_CHANNEL_13,
    PCA9685_CHANNEL_14,
    PCA9685_CHANNEL_15
} pca9685_channel_t;

typedef struct _pca9685_data_t* pca9685_handle_t;

typedef struct _pca9685_data_t {
    TaskHandle_t             driver_task_handle; // Task obsługujący I2C
    i2c_device_config_t      i2c_device_config;  // Zunifikowane nazewnictwo z TPS/INA
    i2c_master_dev_handle_t  i2c_dev_handle;     // Wymaga późniejszego dodania do busa przez usera
    
    uint16_t freq;     
    uint8_t prescale;  
    uint16_t channel_pwm_value[PCA9685_CHANNEL_ALL]; 
    
    /* Maska kanałów do zaktualizowania w tasku (1 bit na kanał) */
    uint16_t pwm_update_mask; 

    uint8_t sub_value[3];
    uint8_t allcalladr;  

    union {
        uint8_t reg_val;
        struct {
            uint8_t ALLCALL : 1;   
            uint8_t SUB3    : 1;   
            uint8_t SUB2    : 1;   
            uint8_t SUB1    : 1;   
            uint8_t SLEEP   : 1;   
            uint8_t AI      : 1;   
            uint8_t EXTCLK  : 1;   
            uint8_t RESTART : 1;   
        };
    } mode1;

    union {
        uint8_t reg_val;
        struct {
            uint8_t OUTNE   : 2;   
            uint8_t OUTDRV  : 1;   
            uint8_t OCH     : 1;   
            uint8_t INVRT   : 1;   
            uint8_t RESERVED: 3;   
        };
    } mode2;

    /* Flagi operacji odłożonych w czasie */
    struct {
        uint16_t update_pwm_duty;
        bool update_mode1;
    } to_update;

} _pca9685_data_t;

/**
 * @brief Create a new instance of PCA9685 driver and its background task
 * @param i2c_address I2C address of the device (default 0x40 or 0x70 for all-call)
 * @return Handle to the device, or NULL if allocation failed
 */
pca9685_handle_t pca9685_new(uint8_t i2c_address);

/**
 * @brief Set PWM value for a channel. 
 * @param immediate If true, blocks and writes I2C directly. If false, schedules update in Task.
 */
esp_err_t pca9685_set_pwm_value(pca9685_handle_t handle, uint8_t channel, uint16_t value, bool immediate);

/**
 * @brief Force update of PWM values selected by bitmask immediately
 */
esp_err_t pca9685_update_pwm_values(pca9685_handle_t handle, uint16_t bitmask);

/**
* @brief Set subaddress value and state
* @param dev_handle handle of device
* @param num address num (0-2)
* @param address_val to set
* @param en shall be enabled to work(1)
* @return esp_err_t ESP_OK on success
*/
esp_err_t pca9685_set_subaddress(pca9685_handle_t handle, uint8_t num, uint8_t address_val, bool en);

/**
* @brief soft restart
* @param dev_handle handle of device
* @return esp_err_t ESP_OK on success
*/
esp_err_t pca9685_restart(pca9685_handle_t handle);


/**
* @brief set sleep
* @param dev_handle handle of device
* @param sleep true if want sleep
* @return esp_err_t ESP_OK on success
*/
esp_err_t pca9685_sleep(pca9685_handle_t handle, bool sleep);


/**
* @brief set outputs logic inverted
* @param dev_handle handle of device
* @param inverted false for normal behaviour
* @return esp_err_t ESP_OK on success
*/
esp_err_t pca9685_set_output_inverted(pca9685_handle_t handle, bool inverted);

/**
* @brief set outputs behaviour 
* @param dev_handle handle of device
* @param od true for open drain false for totempole
* @return esp_err_t ESP_OK on success
*/
esp_err_t pca9685_set_output_open_drain(pca9685_handle_t handle, bool od);

/**
* @brief set global PWM frequency 
* @param dev_handle handle of device
* @param freq frequency (24-1526)Hz
* @return esp_err_t ESP_OK on success
*/
esp_err_t pca9685_set_pwm_frequency(pca9685_handle_t handle, uint16_t freq);


/**
* @brief fetch prescaler and calculate frequency
* @param dev_handle handle of device
* @return esp_err_t ESP_OK on success
*/
esp_err_t pca9685_get_prescaler_and_freq(pca9685_handle_t handle);

/**
* @brief Read register values, store in hadle struct
* @param dev_handle handle of device
* @return esp_err_t ESP_OK on success
*/
esp_err_t pca9685_read_modes_reg(pca9685_handle_t handle);

/**
* @brief Write mode1 or mode2 from handle to device (read first recommended)
* @param dev_handle handle of device
* @param reg mode(1 or 2)
* @return esp_err_t ESP_OK on success
*/
esp_err_t pca9685_write_modes_reg(pca9685_handle_t handle, uint8_t reg);


esp_err_t pca9685_enable_auto_increment(pca9685_handle_t handle);