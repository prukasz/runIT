#pragma once 
#include <stdint.h>
#include <stdbool.h>
#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>
#include <freertos/task.h>

/* =========================================================================
 * STATUS & ERROR MANAGEMENT MACROS
 * =========================================================================
 * * --- ERROR GENERATION ---
 * STA_ERR_X()                : Create fully custom error struct
 * STA_ERR_S_I()             : Create INFO level error struct
 * STA_ERR_S_W()             : Create WARNING level error struct
 * STA_ERR_S_C()             : Create CRITICAL level error struct
 * STA_ERR_OK()               : Empty/Success error struct (Code 0)
 * STA_ERR_FROM_ESP()         : Convert standard esp_err_t to Critical error
 * * --- ERROR CHECKING ---
 * STA_IS_OK()                : Evaluates to true if error code is 0
 * STA_IS_ERR()               : Evaluates to true if error code is NOT 0
 * * --- ERROR HANDLING & ROUTING ---
 * STA_PASS_ERR()              : Increment error depth and return struct
 * STA_ERR_ADD_TO_STREAM()    : Push error to Ring Buffer (if flags allow)
 * * --- FLOW CONTROL (RETURNS) ---
 * STA_RETURN_ERR_OK        : Immediately return an OK/0 struct
 * STA_RETURN_ON_ERR        : Immediately return if error exists
 * STA_ERR_RETURN_PUSH      : If error: push to buffer, then return it
 * STA_ERR_RETURN_PUSH_LOG  : If error: log it, push to buffer, then return it
 * ========================================================================= */

typedef struct{
    uint8_t log_i:1;
    uint8_t log_w:1;
    uint8_t log_c:1;
    uint8_t rep_i:1;
    uint8_t rep_w:1;
    uint8_t rep_c:1;
    uint8_t _reserved:2;
}status_manager_flags_t;

typedef struct{
    EventGroupHandle_t events;
    EventBits_t bits_task_run;
    EventBits_t bits_task_done;
    uint32_t task_stack_size; 
    uint8_t task_priority;
}m_status_cfg_t;

extern status_manager_flags_t s_err_manager_flags;
extern RingbufHandle_t s_err_buffer_handle;
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
    ERR_ESP_ERR_MEMPROT_BASE        = 0xd000  /*!< Starting number of Memory Protection API error codes */

} status_err_code_e;


typedef enum{
    OWN_a_i2c_init,
    OWN_m_i2c_init,
    OWN_m_i2c_add_driver,
}status_owner_e;

typedef struct{
    status_err_code_e code;
    uint16_t origin_id;   //idx of block for example//
    uint16_t origin_name; //enum//
    uint16_t node_id;
    struct{
        uint8_t severity:2;
        uint8_t my_depth:6;
        uint8_t  _reserved;
    }details;
}status_err_report_t;

typedef enum{
    STATUS_rep_X
}status_code_e;

typedef struct{
    uint16_t code; //vm_status_code_e 
    uint16_t origin_id;   //idx of block for example
    uint16_t node_id;
    uint16_t origin_name; //enum//
}status_report_t;

#define STA_ERR_X(err_code, oid, oname, nid, sev, dep) \
    (status_err_report_t){ \
        .code = (err_code), \
        .origin_id = (oid), \
        .origin_name = (oname), \
        .node_id = (nid), \
        .details = { .severity = (sev), .my_depth = (dep), ._reserved = 0 } \
    }

#define STA_ERR_S_I(err_code, name) STA_ERR_X((err_code), 0, (name), 0, 0, 0)
#define STA_ERR_S_W(err_code, name) STA_ERR_X((err_code), 0, (name), 0, 1, 0)
#define STA_ERR_S_C(err_code, name) STA_ERR_X((err_code), 0, (name), 0, 2, 0)

static inline status_err_report_t STA_PASS_ERR(status_err_report_t err) {
    if (err.code != 0) { 
        err.details.my_depth++;
    }
    return err;
}

static inline void STA_ERR_PUSH_TO_BUFFER_OVERWRITE(RingbufHandle_t rb, const status_err_report_t *item) {
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

#define STA_ERR_ADD_TO_STREAM(err) do { \
    status_err_report_t _sta_err = (err); \
    if (s_err_buffer_handle != NULL) { \
        if (((_sta_err.details.severity == 0) && s_err_manager_flags.rep_i) || \
            ((_sta_err.details.severity == 1) && s_err_manager_flags.rep_w) || \
            ((_sta_err.details.severity == 2) && s_err_manager_flags.rep_c)) { \
            STA_ERR_PUSH_TO_BUFFER_OVERWRITE(s_err_buffer_handle, &_sta_err); \
        } \
    } \
} while (0)

#define STA_ERR_OK ((status_err_report_t){0})
#define STA_RETURN_ERR_OK() do { return STA_ERR_OK; } while (0)
#define STA_IS_OK(err)  ((err).code == 0)
#define STA_IS_ERR(err) ((err).code != 0)

#define STA_ERR_FROM_ESP(esp_error, origin_enum) ({ \
    esp_err_t _e = (esp_error); \
    status_err_report_t _rep = STA_ERR_OK; \
    if (_e != ESP_OK) { \
        _rep = STA_ERR_S_C(_e, (origin_enum)); \
    } \
    _rep; \
})


#define STA_RETURN_ON_ERR(err) do { \
    status_err_report_t _sta_err = (err); \
    if (_sta_err.code != 0) { \
        return _sta_err; \
    } \
} while (0)

#define STA_ERR_RETURN_PUSH(err) ({ \
    status_err_report_t _sta_err = (err); \
    if (_sta_err.code != 0) { \
        STA_ERR_ADD_TO_STREAM(_sta_err); \
        return _sta_err; \
    } \
    _sta_err; \
})


#define STA_ERR_RETURN_PUSH_LOG(err, fmt, ...) ({ \
    status_err_report_t _sta_err = (err); \
    if (_sta_err.code != 0) { \
        STA_ERR_ADD_TO_STREAM(_sta_err); \
        if ((_sta_err.details.severity == 0) && s_err_manager_flags.log_i) { \
            ESP_LOGI(__FILE_NAME__, "[%s] " fmt, __func__, ##__VA_ARGS__); \
        } else if ((_sta_err.details.severity == 1) && s_err_manager_flags.log_w) { \
            ESP_LOGW(__FILE_NAME__, "[%s] " fmt, __func__, ##__VA_ARGS__); \
        } else if ((_sta_err.details.severity == 2) && s_err_manager_flags.log_c) { \
            ESP_LOGE(__FILE_NAME__, "[%s] " fmt, __func__, ##__VA_ARGS__); \
        } \
        return _sta_err; \
    } \
    _sta_err; \
})

void s_manager_init(EventGroupHandle_t events_to_set, EventGroupHandle_t events_to_wait, uint32_t set_bits, 
    uint32_t wait_bits, RingbufHandle_t buffer);

TaskHandle_t s_manager_get_task_handle(void);
RingbufHandle_t s_manager_get_buffer_handle(void);

void s_manager_cgf_i(bool en_log, bool en_rep);
void s_manager_cgf_w(bool en_log, bool en_rep);
void s_manager_cgf_c(bool en_log, bool en_rep);
