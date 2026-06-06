#pragma once 
#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/ringbuf.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <stddef.h>
#include "status_codes.h"
#include "freertos/queue.h"


typedef struct{
    uint8_t rep_i:1;
    uint8_t rep_w:1;
    uint8_t rep_c:1;
    uint8_t _reserved:2;
}status_manager_log_cfg;


/**
 * @brief Remote error / status report struct
 * @param e_code: UINT16_t Error code (status_err_code_e)
 * @param e_owner: UINT32_t Enum for error origin (status_owner_e)
 * @param track: Union for error tracking info (e.g. for for more info about owner)
 * @param details: (bitfield) severity (0-Info, 1-Warning, 2-Critical)
 */
typedef struct{
    uint32_t e_code;    //
    uint32_t e_owner;   //4
    union 
    {
        struct {
            uint32_t ui_block_id;     
            uint32_t ui_node_id;     
        }vm;
        uint64_t origin_info; //8
    }track;
       //2
    struct{
        uint8_t   severity : 2;
        uint8_t  _reserved : 6;
    }details;                //2
}status_rep_t;

void _sta_push_overwrite(const status_rep_t *item);

/**
 * @brief Non VM version
 */
#define _STA_X(_code, _owner, _origin_info, _severity) \
    (status_rep_t){ \
        .e_code = (_code), \
        .e_owner = (_owner), \
        .track = { .origin_info = (_origin_info) }, \
        .details = { .severity = (_severity), ._reserved = 0 } \
    }



#define STA_OK ((status_rep_t){0})
#define STA_I(code, e_owner, origin_info) _STA_X((code), (e_owner), (origin_info), 0)
#define STA_W(code, e_owner, origin_info) _STA_X((code), (e_owner), (origin_info), 1)
#define STA_C(code, e_owner, origin_info) _STA_X((code), (e_owner), (origin_info), 2)

/* Checking macros */
#define STA_IS_OK(err)  (((err).e_code == 0) || ((err).details.severity == 0))
#define STA_IS_ERR(err)  (((err).e_code != 0) && ((err).details.severity > 0))


/**
 * @brief Macro to push a status_rep_t to the buffer
 */
#define STA_P(status) do { \
    status_rep_t _sta_p = (status); \
    _sta_push_overwrite(&_sta_p); \
} while(0)

/**
 * @brief Return and Push if error
 */
#define STA_RP(status) do { \
    status_rep_t _sta_rp = (status); \
    STA_P(_sta_rp); \
    return _sta_rp; \
} while(0)

/**
 * @brief Return if status is error, otherwise continue
 */
#define STA_R_ON_ERR(status) do { \
    status_rep_t _sta_check = (status); \
    if (_sta_check.e_code != 0) { \
        return _sta_check; \
    } \
} while(0)

/**
 * @brief Push status to buffer if it's an error,cuontinue anyway
 */
#define STA_P_ON_ERR(status) do { \
    status_rep_t _sta_p_check = (status); \
    if (_sta_p_check.e_code != 0) { \
        STA_P(_sta_p_check); \
    } \
} while(0)

/**
 * @brief Push status to buffer if it's an error and return, otherwise continue
 */
#define STA_RP_ON_ERR(status) do { \
    status_rep_t _sta_rp_check = (status); \
    if (_sta_rp_check.e_code != 0) { \
        STA_RP(_sta_rp_check); \
    } \
} while(0)




/**
 * @brief Macro to wrap an esp_err_t call and convert it to status_rep_t
 * @note OWNER required
 * @note esp_err_t is stored in origin_info
 */
#define STA_FROM_ESP(esp_err_expr) ({ \
    esp_err_t _esp_err = (esp_err_expr); \
    status_rep_t _mapped_sta = STA_OK; \
    if (_esp_err != ESP_OK) { \
        _mapped_sta = STA_C(ERR_ESP, OWNER, _esp_err); \
    } \
    _mapped_sta; \
})

/**
 * @brief Macro to check if status is ESP_ERR, mark as warning and add to buffer
 * @note OWNER required, overrides severity to warning, return true if it's an ESP_ERR and was pushed to buffer, false otherwise
 */
#define STA_P_ON_ESP_ERR(status) ({ \
    __typeof__(status) _s = (status); \
    bool _is_esp_err = (_s.e_code == ERR_ESP); \
    if (_is_esp_err) { \
        _s.details.severity = 1; \
        STA_P(_s); \
    } \
    _is_esp_err; \
})



/**
 * @brief check if handle exists if not return error 
 * @note OWNER required, returns ERR_MISSING_HANDLE if handle is NULL,
 * @note LOGGING
 */
#define CHECK_HANDLE_R(handle) do { \
    if ((handle) == NULL) { \
        ESP_LOGE(__FILE_NAME__, "%s: No device handle for '%s'", __func__, #handle); \
        return STA_C(ERR_MISSING_HANDLE, OWNER, 0); \
    } \
} while(0)

/**
 * @brief check if ptr exists if not return error 
 * @note OWNER required, returns ERR_INVALID_ARG if ptr is NULL,
 * @note LOGGING
 */
#define CHECK_NOT_NULL_R(ptr) do { \
    if ((ptr) == NULL) { \
        ESP_LOGE(__FILE_NAME__, "%s: Pointer '%s' is NULL", __func__, #ptr); \
        return STA_C(ERR_INVALID_ARG, OWNER, 0); \
    } \
} while(0)

/**
 * @brief check if arg in range else return error
 * @note OWNER required, returns ERR_INVALID_ARG if out of range,
 * @note LOGGING 
 */
#define CHECK_ARG_R(arg, min_val, max_val, override_return) do { \
    __typeof__(arg) _a = (arg); \
    __typeof__(min_val) _min = (min_val); \
    __typeof__(max_val) _max = (max_val); \
    \
    if (_a < _min || _a > _max) { \
        ESP_LOGE(__FILE_NAME__, "%s: Argument '%s' out of range [%lld, %lld] (Got: %lld)", \
                 __func__, #arg, (int64_t)_min, (int64_t)_max, (int64_t)_a); \
        return STA_C(ERR_INVALID_ARG, OWNER, (override_return) ?  (int64_t)(override_return) : (int64_t)_a); \
    } \
} while(0)

/**
 * @brief check if arg in range else return error and push to buffer
 * @note OWNER required, returns ERR_INVALID_ARG if out of range,
 * @note LOGGING
 */
#define CHECK_ARG_RP(arg, min_val, max_val, override_return) do { \
    __typeof__(arg) _a = (arg); \
    __typeof__(min_val) _min = (min_val); \
    __typeof__(max_val) _max = (max_val); \
    \
    if (_a < _min || _a > _max) { \
        ESP_LOGE(__FILE_NAME__, "%s: Argument '%s' out of range [%lld, %lld] (Got: %lld)", \
                 __func__, #arg, (int64_t)_min, (int64_t)_max, (int64_t)_a); \
        STA_RP(STA_C(ERR_INVALID_ARG, OWNER, (override_return) ?  (int64_t)(override_return) : (int64_t)_a)); \
    } \
} while(0)

/**
 * @brief check if is returned esp_err if yes then wrap into status and return
 * @note OWNER required, returns ERR_ESP if esp_err is not ESP_OK,
 * @note LOGGING
 */
#define CHECK_ESP_CALL_R(esp_err_call) do { \
    esp_err_t _err = (esp_err_call); \
    if (_err != ESP_OK) { \
        ESP_LOGE(__FILE_NAME__, "%s: ESP API Failed '%s' -> %s (0x%x)", \
                 __func__, #esp_err_call, esp_err_to_name(_err), _err); \
        return STA_C(ERR_ESP, OWNER, _err); \
    } \
} while(0)

/**
 * @brief check if is returned esp_err if yes then wrap into status and return
 * @note OWNER required, returns ERR_ESP if esp_err is not ESP_OK,
 * @note LOGGING
 */
#define CHECK_ESP_CALL_P(esp_err_call) do { \
    esp_err_t _err = (esp_err_call); \
    if (_err != ESP_OK) { \
        ESP_LOGE(__FILE_NAME__, "%s: ESP API Failed '%s' -> %s (0x%x)", \
                 __func__, #esp_err_call, esp_err_to_name(_err), _err); \
        STA_P(STA_C(ERR_ESP, OWNER, _err)); \
    } \
} while(0)

/**
 * @brief check if is returned esp_err if yes then wrap into status and return
 * @note OWNER required, returns ERR_ESP if esp_err is not ESP_OK,
 * @note LOGGING
 */
#define CHECK_ESP_CALL_RP(esp_err_call) do { \
    esp_err_t _err = (esp_err_call); \
    if (_err != ESP_OK) { \
        ESP_LOGE(__FILE_NAME__, "%s: ESP API Failed '%s' -> %s (0x%x)", \
                 __func__, #esp_err_call, esp_err_to_name(_err), _err); \
        STA_RP(STA_C(ERR_ESP, OWNER, _err)); \
    } \
} while(0)


/**
 * clamp to min max
 */
#define CLAMP(val, min_val, max_val) ({ \
    __typeof__(val) _val = (val); \
    __typeof__(min_val) _min = (min_val); \
    __typeof__(max_val) _max = (max_val); \
    (_val < _min) ? (__typeof__(val))_min : \
    (_val > _max) ? (__typeof__(val))_max : _val; \
})



void status_assign_buffer(RingbufHandle_t status_buffer, QueueHandle_t status_queue);
void status_set_rep_mode(bool rep_i, bool rep_w, bool rep_c);
#define STATUS_SUSPEND() status_set_rep_mode(0,0,0)
#define STATUS_RESUME()  status_set_rep_mode(1,1,1)
void status_mutex_lock();
void status_mutex_unlock();