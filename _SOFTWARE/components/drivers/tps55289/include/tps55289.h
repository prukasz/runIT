#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TPS55289_I2C_ADDR_74 0x74
#define TPS55289_I2C_ADDR_75 0x75

// Liczba dostępnych rejestrów (0x00 do 0x07)
#define TPS55289_REG_MAX 0x08

// Rejestry TPS55289
#define TPS55289_REG_REF_LSB     0x00
#define TPS55289_REG_REF_MSB     0x01
#define TPS55289_REG_IOUT_LIMIT  0x02
#define TPS55289_REG_VOUT_SR     0x03
#define TPS55289_REG_VOUT_FS     0x04
#define TPS55289_REG_CDC         0x05
#define TPS55289_REG_MODE        0x06
#define TPS55289_REG_STATUS      0x07

/**
 * @brief Typy błędów obsługiwane przez osobne callbacki
 */
typedef enum {
    TPS55289_FAULT_OVP, // Over-Voltage Protection
    TPS55289_FAULT_OCP, // Over-Current Protection
    TPS55289_FAULT_SCP  // Short-Circuit Protection
} tps55289_fault_type_t;

typedef struct
{
    /* Driver task handle for alerts and deferred operations */
    TaskHandle_t driver_task_handle;
    
    /* I2C device configuration filled up automatically */
    i2c_device_config_t i2c_device_config;
    
    /* I2C master device handle, needs to be initialized/added to bus by user */
    i2c_master_dev_handle_t i2c_master_dev_handle;

    uint16_t shunt_resistor_mohm; //10mohm default,

    /**
     * @brief Cache of register values to minimize I2C reads
     */
    uint8_t reg_cache[TPS55289_REG_MAX];

    /**
     * @brief Readings data stored, and updated by deferred reading operations in task.
     */
    struct
    {
        uint8_t raw_status_reg;
        bool scp;       // Short-Circuit Protection triggered
        bool ocp;       // Over-Current Protection triggered
        bool ovp;       // Over-Voltage Protection triggered
        uint8_t op_mode; // 00=Buck, 01=Boost, 10=Buck-Boost
    } last_status;

    /**
     * @brief User-defined callbacks for specific fault types
     */
    void (*user_callback_ovp)(void *arg);
    void *user_callback_arg_ovp;

    void (*user_callback_ocp)(void *arg);
    void *user_callback_arg_ocp;

    void (*user_callback_scp)(void *arg);
    void *user_callback_arg_scp;

    /* Flag for INT/Fault pin triggered - signals the task to check status */
    bool alert_fault;

    /**
     * @brief Flags for deferred operation in task
     */
    struct {
        uint8_t read_status          : 1;
        uint8_t read_status_periodic : 1;
    } to_update;

} _tps55289_data_t;

typedef _tps55289_data_t* tps55289_handle_t;

/**
 * @brief Initialize a new TPS55289 device descriptor and its task
 * @param i2c_address Device I2C address (usually 0x74 or 0x75)
 * @return Handle to the TPS55289 instance or NULL on failure
 */
tps55289_handle_t tps55289_new(uint8_t i2c_address);

/**
 * @brief Free the TPS55289 device descriptor and delete its task
 */
void tps55289_delete(tps55289_handle_t handle);

/**
 * @brief set shunt 
 */
void tps55289_set_shunt_resistor(tps55289_handle_t handle, uint16_t resistance_mOhm);

/**
 * @brief Synchronizuje all registiers
 */
esp_err_t tps55289_sync_all_registers(tps55289_handle_t handle);

/**
 * @brief On or Off of output stage (OE)
 */
esp_err_t tps55289_set_output_enable(tps55289_handle_t handle, bool enable);

/**
 * @brief Set current limit based on desired mA and shunt resistor value.
 * @param enable Is current limit enabled (devide tries to hold the limit and drops voltage)
 * @param limit_ma Current limit in mA
 */
esp_err_t tps55289_set_current_limit(tps55289_handle_t handle, bool enable, uint16_t limit_ma);

/**
 * @brief Set the output voltage 
 * @param voltage_mv Target voltage in millivolts
 */
esp_err_t tps55289_set_voltage(tps55289_handle_t handle, uint16_t voltage_mv);

/**
 * @brief Working mode
 * @param fpwm true = Forced PWM, false = Auto PFM/PWM
 * @param hiccup true = shut down and retry in a while, else latch-off
 */
esp_err_t tps55289_set_mode(tps55289_handle_t handle, bool fpwm, bool hiccup);

/**
 * @brief Activate or deactivate fault masks for SCP, OCP, OVP. Mask ignore the fault condition on status register and alert pin.
 */
esp_err_t tps55289_set_fault_masks(tps55289_handle_t handle, bool mask_scp, bool mask_ocp, bool mask_ovp);

// =========================================================================

esp_err_t tps55289_get_status(tps55289_handle_t handle, bool immediate);
void tps55289_cfg_periodic_reading(tps55289_handle_t handle, bool status);
void tps55289_register_user_callback(tps55289_handle_t handle, tps55289_fault_type_t type, void (*callback)(void *), void *arg);
void tps55289_isr_callback_fault(void *arg);