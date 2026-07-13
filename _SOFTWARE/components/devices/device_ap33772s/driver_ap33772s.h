#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_i2c.h"

#define AP33772S_ADDRESS 0x52
#define MAX_PDO_ENTRIES  13

// Software Voltage Limit Setup (Safe limit to 22V)
#define AP33772S_MAX_SOFTWARE_VOLTAGE_MV 22000

// Registers
#define CMD_STATUS    0x01
#define CMD_MASK      0x02
#define CMD_OPMODE    0x03
#define CMD_CONFIG    0x04
#define CMD_PDCONFIG  0x05
#define CMD_SYSTEM    0x06

#define CMD_TR25      0x0C 
#define CMD_TR50      0x0D 
#define CMD_TR75      0x0E
#define CMD_TR100     0x0F

#define CMD_VOLTAGE   0x11
#define CMD_CURRENT   0x12
#define CMD_TEMP      0x13
#define CMD_VREQ      0x14
#define CMD_IREQ      0x15

#define CMD_VSELMIN   0x16 
#define CMD_UVPTHR    0x17
#define CMD_OVPTHR    0x18
#define CMD_OCPTHR    0x19
#define CMD_OTPTHR    0x1A
#define CMD_DRTHR     0x1B

#define CMD_SRCPDO    0x20

#define CMD_PD_REQMSG 0x31
#define CMD_PD_CMDMSG 0x32
#define CMD_PD_MSGRLT 0x33

// Bitfields packed safely for GCC/ESP32 architectures
typedef struct {
    union {
        struct {
            unsigned int voltage_max: 8;   
            unsigned int peak_current: 2;  
            unsigned int current_max: 4;   
            unsigned int type: 1;          
            unsigned int detect: 1;        
        } fixed;
        struct {
            unsigned int voltage_max: 8;   
            unsigned int voltage_min: 2;   
            unsigned int current_max: 4;   
            unsigned int type: 1;          
            unsigned int detect: 1;        
        } pps;
        struct {
            unsigned int voltage_max: 8;   
            unsigned int voltage_min: 2;   
            unsigned int current_max: 4;   
            unsigned int type: 1;          
            unsigned int detect: 1;        
        } avs;
        struct {
            uint8_t byte0;
            uint8_t byte1;
        };
    };
    uint32_t data;
} src_spr_and_epr_pdo_fields_t;

typedef struct {
    union {
        struct {
            unsigned int VOLTAGE_SEL: 8;  
            unsigned int CURRENT_SEL: 4;  
            unsigned int PDO_INDEX: 4;  
        } REQMSG_Fields;
        struct {
            uint8_t byte0;
            uint8_t byte1;
        };
    };
    uint32_t data;
} rdo_data_t;

typedef struct {
    // 1. INHERITANCE: Klasa bazowa na samym początku!
    sys_i2c_driver_header_t header;

    TaskHandle_t driver_task;

    int index_pps_user;
    int index_avs_user;

    // Internal states cached for the periodic AVS keep-alive loop
    volatile bool avs_active;
    int index_avs_cache;
    int voltage_avs_byte_cache;
    int current_avs_byte_cache;

    // Interrupt handling mapped to FreeRTOS Deferred Task Execution
    void (*user_isr_callback)(void *arg);
    void *user_isr_arg;
    volatile bool interrupt_triggered;

    src_spr_and_epr_pdo_fields_t src_pdo_array[MAX_PDO_ENTRIES];
} _ap33772s_data_t;

typedef _ap33772s_data_t* ap33772s_handle_t;

/**
 * @brief (Faza 1) Initialize a new AP33772S device instance configuration.
 * @param i2c_bus_num Numer magistrali I2C (0 lub 1)
 */
ap33772s_handle_t ap33772s_new(bool i2c_bus_num);

/**
 * @brief (Faza 2) Register to I2C manager and start the background keep-alive task.
 */
esp_err_t ap33772s_start(ap33772s_handle_t handle);

/**
 * @brief Free driver instance resource.
 */
void ap33772s_delete(ap33772s_handle_t handle);

/**
 * @brief Reads available Source PDO definitions from the chip and evaluates profiles.
 */
esp_err_t ap33772s_begin(ap33772s_handle_t handle);

/**
 * @brief Dump parsed target power profiles straight to ESP console output.
 */
void ap33772s_log_profiles(ap33772s_handle_t handle);

/**
 * @brief Request Fixed Power Profile.
 */
esp_err_t ap33772s_set_fixed_pdo(ap33772s_handle_t handle, int pdo_index, int max_current_ma);

/**
 * @brief Request Programmable Power Supply (PPS) dynamic rail.
 */
esp_err_t ap33772s_set_pps_pdo(ap33772s_handle_t handle, int pdo_index, int target_voltage_mv, int max_current_ma);

/**
 * @brief Request Adjustable Voltage Supply (AVS) profile. Handles keep-alives automatically.
 */
esp_err_t ap33772s_set_avs_pdo(ap33772s_handle_t handle, int pdo_index, int target_voltage_mv, int max_current_ma);

/**
 * @brief Enable or disable VBUS Output switch stage.
 */
esp_err_t ap33772s_set_output(ap33772s_handle_t handle, bool enable);

// Telemetry Accessors
int ap33772s_read_temp(ap33772s_handle_t handle);
int ap33772s_read_voltage(ap33772s_handle_t handle);
int ap33772s_read_current(ap33772s_handle_t handle);
int ap33772s_read_vreq(ap33772s_handle_t handle);
int ap33772s_read_ireq(ap33772s_handle_t handle);

// Dynamic Parameters
esp_err_t ap33772s_set_ntc(ap33772s_handle_t handle, int tr25, int tr50, int tr75, int tr100);
int ap33772s_read_vselmin(ap33772s_handle_t handle);
esp_err_t ap33772s_set_vselmin(ap33772s_handle_t handle, int voltage_mv);
int ap33772s_read_uvp_threshold(ap33772s_handle_t handle);
esp_err_t ap33772s_set_uvp_threshold(ap33772s_handle_t handle, int percentage);
int ap33772s_read_ovp_threshold(ap33772s_handle_t handle);
esp_err_t ap33772s_set_ovp_threshold(ap33772s_handle_t handle, int voltage_mv);

// Interrupt Control 
uint8_t ap33772s_read_status(ap33772s_handle_t handle);
void ap33772s_register_interrupt(ap33772s_handle_t handle, void (*callback)(void *), void *arg);
void ap33772s_intr_handler(void *arg);