#pragma once 
#include <stdint.h>
#include <stdbool.h>
#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>
#include <freertos/task.h>

/* =========================================================================
 * STATUS & ERROR MANAGEMENT MACROS
 * ========================================================================= */

typedef struct{
    EventGroupHandle_t events;
    EventBits_t bits_task_run;
    EventBits_t bits_task_done;
    uint32_t task_stack_size; 
    uint8_t task_priority;
}m_status_cfg_t;

typedef struct{
    uint8_t log_i:1;
    uint8_t log_w:1;
    uint8_t log_c:1;
    uint8_t rep_i:1;
    uint8_t rep_w:1;
    uint8_t rep_c:1;
    uint8_t _reserved:2;
}status_manager_log_cfg;


typedef enum {
    /* --- Standard ESP-IDF Success/Fail (Prefixed) --- */
    ERR_ESP_FAIL                    = -1,     /*!< Generic esp_err_t code indicating failure */
    ERR_ESP_OK                      = 0,      /*!< esp_err_t value indicating success (no error) */

    /* --- Custom App & I2C Error Codes --- */
    ERR_OK                          = 0,      /*!< Alias for success */
    ERR_FAIL                        = 1,      /*!< Custom generic failure */
    ERR_I2C_NOT_FOUND               = 2,
    ERR_I2C_TRANSMIT_FAIL           = 3,
    ERR_I2C_RECEIVE_FAIL            = 4,
    ERR_I2C_DEVICE_NOT_PRESENT      = 5,
    ERR_I2C_SCAN_FAIL               = 6,
    ERR_I2C_ADD_DEVICE_FAIL         = 7,
    ERR_I2C_REMOVE_DEVICE_FAIL      = 8,
    ERR_NO_MEM                      = 9,      /*!< Custom out of memory */
    ERR_INVALID_ARG                 = 10,     /*!< Custom invalid argument */
    ERR_I2C_DEV_REG_FULL,  
    ERR_I2C_DEV_ADD_FAIL,      
    ERR_I2C_DEV_MNT_F,   
    /* --- Standard ESP-IDF Error Codes (Prefixed) --- */
    ERR_ESP_ERR_NO_MEM              = 0x101,  /*!< Out of memory */
    ERR_ESP_ERR_INVALID_ARG         = 0x102,  /*!< Invalid argument */
    ERR_ESP_ERR_INVALID_STATE       = 0x103,  /*!< Invalid state */
    ERR_ESP_ERR_INVALID_SIZE        = 0x104,  /*!< Invalid size */
    ERR_ESP_ERR_NOT_FOUND           = 0x105,  /*!< Requested resource not found */
    ERR_ESP_ERR_NOT_SUPPORTED       = 0x106,  /*!< Operation or feature not supported */
    ERR_ESP_ERR_TIMEOUT             = 0x107,  /*!< Operation timed out */
    ERR_ESP_ERR_INVALID_RESPONSE    = 0x108,  /*!< Received response was invalid */
    ERR_ESP_ERR_INVALID_CRC         = 0x109,  /*!< CRC or checksum was invalid */
    ERR_ESP_ERR_INVALID_VERSION     = 0x10A,  /*!< Version was invalid */
    ERR_ESP_ERR_INVALID_MAC         = 0x10B,  /*!< MAC address was invalid */
    ERR_ESP_ERR_NOT_FINISHED        = 0x10C,  /*!< Operation has not fully completed */
    ERR_ESP_ERR_NOT_ALLOWED         = 0x10D,  /*!< Operation is not allowed */

    /* --- Standard ESP-IDF Base Error Codes (Prefixed) --- */
    ERR_ESP_ERR_WIFI_BASE           = 0x3000, /*!< Starting number of WiFi error codes */
    ERR_ESP_ERR_MESH_BASE           = 0x4000, /*!< Starting number of MESH error codes */
    ERR_ESP_ERR_FLASH_BASE          = 0x6000, /*!< Starting number of flash error codes */
    ERR_ESP_ERR_HW_CRYPTO_BASE      = 0xc000, /*!< Starting number of HW cryptography module error codes */
    ERR_ESP_ERR_MEMPROT_BASE        = 0xd000,  /*!< Starting number of Memory Protection API error codes */
    STA_I2C_INITILAIZED, 
    STA_I2C_DRIVER_ADDED,
    ERR_I2C_DEV_NOT_FOUND,

    
} status_err_code_e;


typedef enum{
    OWN_a_i2c_init,
    OWN_m_i2c_init,
    OWN_m_i2c_add_driver,
    OWN_m_i2c_enqueue_aperiodic_job,
}status_owner_e;


/**
 * @brief Remote error / status report struct
 * @param e_code: UINT16_t Error code (status_err_code_e)
 * @param e_owner: UINT16_t Enum for error origin (status_owner_e)
 * @param track: Union for error tracking info (e.g. for for more info about owner)
 * @param details: (bitfield) severity (0-Info, 1-Warning, 2-Critical), depth (for error propagation tracking)
 */
typedef struct{
    uint16_t e_code;    //4
    uint16_t e_owner;   //2
    union 
    {
        struct {
            uint16_t ui_block_id;     
            uint16_t ui_node_id;     
        }vm;
        uint32_t origin_info; //4
    }track;
       //2
    struct{
        uint8_t severity:2;
        uint8_t my_depth:6;
        uint8_t  _reserved;
    }details;                //2
}status_err_report_t;

extern RingbufHandle_t _status_buffer_handle;
extern status_manager_log_cfg _status_log_flags;


static __always_inline void _sta_push_overwrite(RingbufHandle_t rb, const status_err_report_t *item) {
    if (rb == NULL) {
        return;
    }

    if (xRingbufferSend(rb, item, sizeof(*item), 0) != pdTRUE) {
        size_t old_size = 0;
        void *old_item = xRingbufferReceive(rb, &old_size, 0);
        if (old_item != NULL) {
            vRingbufferReturnItem(rb, old_item);
            (void)xRingbufferSend(rb, item, sizeof(*item), 0);
        }
    }
}


/*Error struct creation - for non VM scenarios*/
#define _STA_X(_code, _owner, _origin_info, _severity, _depth) \
    (status_err_report_t){ \
        .e_code = (_code), \
        .e_owner = (_owner), \
        .track = { .origin_info = (_origin_info) }, \
        .details = { .severity = (_severity), .my_depth = (_depth), ._reserved = 0 } \
    }
/*Error struct creation - for non VM scenarios*/


/*base error macros*/
#define STA_OK ((status_err_report_t){0})
#define STA_I(code, e_owner, origin_info) _STA_X((code), (e_owner), (origin_info), 0, 0)
#define STA_E(code, e_owner, origin_info) _STA_X((code), (e_owner), (origin_info), 1, 0)
#define STA_C(code, e_owner, origin_info) _STA_X((code), (e_owner), (origin_info), 2, 0)
/*base error macros*/

/*Checking macros*/
#define STA_IS_ERR(err) (((err).e_code != 0) && ((err).details.severity > 0))
#define STA_IS_OK(err)  (((err).e_code == 0) || ((err).details.severity == 0))
/*Checking macros*/

/*give error with incremented depth by 1*/
#define STA_PASS_ERR(err) ({ \
    status_err_report_t _err_copy = (err); \
    if (STA_IS_ERR(_err_copy)) { \
        _err_copy.details.my_depth++; \
    } \
    _err_copy; \
})


/*Push error to buffer*/
#define STA_PUSH(err) do { \
    status_err_report_t _sta_err = (err); \
    if (_status_buffer_handle != NULL) { \
        if (((_sta_err.details.severity == 0) && _status_log_flags.rep_i) || \
            ((_sta_err.details.severity == 1) && _status_log_flags.rep_w) || \
            ((_sta_err.details.severity == 2) && _status_log_flags.rep_c)) { \
            _sta_push_overwrite(_status_buffer_handle, &_sta_err); \
        } \
    } \
} while (0)


/*Push error to buffer and return*/
#define STA_RET_PUSH(err) ({ \
    status_err_report_t _sta_err = (err); \
    STA_PUSH(_sta_err); \
    return _sta_err; \
})

/*Push error to buffer, ESP_LOG, and return*/
#define STA_RET_PUSH_LOG(err, fmt, ...) ({ \
    status_err_report_t _sta_err = (err); \
    if (_sta_err.e_code != 0) { \
        STA_PUSH(_sta_err); \
        if ((_sta_err.details.severity == 0) && _status_log_flags.log_i) { \
            ESP_LOGI(__FILE_NAME__, "[%s] " fmt, __func__, ##__VA_ARGS__); \
        } else if ((_sta_err.details.severity == 1) && _status_log_flags.log_w) { \
            ESP_LOGW(__FILE_NAME__, "[%s] " fmt, __func__, ##__VA_ARGS__); \
        } else if ((_sta_err.details.severity == 2) && _status_log_flags.log_c) { \
            ESP_LOGE(__FILE_NAME__, "[%s] " fmt, __func__, ##__VA_ARGS__); \
        } \
    } \
    return _sta_err; \
})

void status_manager_init(m_status_cfg_t* events, RingbufHandle_t status_buffer);

TaskHandle_t status_manager_get_task_handle(void);

void status_manager_cgf_i(bool en_log, bool en_rep);
void status_manager_cgf_w(bool en_log, bool en_rep);
void status_manager_cgf_c(bool en_log, bool en_rep);
