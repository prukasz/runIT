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





typedef struct{
    uint8_t log_i:1;
    uint8_t log_w:1;
    uint8_t log_c:1;
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
 * @param details: (bitfield) severity (0-Info, 1-Warning, 2-Critical), depth (for error propagation tracking)
 */
typedef struct{
    uint32_t e_code;    //
    uint32_t e_owner;   //4
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
}status_rep_t;

void _sta_push_overwrite(const status_rep_t *item);

/*Error struct creation - for non VM scenarios*/
#define _STA_X(_code, _owner, _origin_info, _severity, _depth) \
    (status_rep_t){ \
        .e_code = (_code), \
        .e_owner = (_owner), \
        .track = { .origin_info = (_origin_info) }, \
        .details = { .severity = (_severity), .my_depth = (_depth), ._reserved = 0 } \
    }
/*Error struct creation - for non VM scenarios*/



#define STA_OK ((status_rep_t){0})
#define STA_I(code, e_owner, origin_info) _STA_X((code), (e_owner), (origin_info), 0, 0)
#define STA_E(code, e_owner, origin_info) _STA_X((code), (e_owner), (origin_info), 1, 0)
#define STA_C(code, e_owner, origin_info) _STA_X((code), (e_owner), (origin_info), 2, 0)

/* Checking macros */
#define STA_IS_OK(err)  (((err).e_code == 0) || ((err).details.severity == 0))

/**
 * @brief Macro to check a status_rep_t for error, if error is present, increment depth and return the status_rep_t
 * @param status The status_rep_t to check
 */
#define STA_RET_ON_ERR(status) do { \
    status_rep_t _sta_check = (status); \
    if (!STA_IS_OK(_sta_check)) { \
        _sta_check.details.my_depth++; \
        return _sta_check; \
    } \
} while(0)


#define STA_RET_ON_ESP_ERR(esp_err_expr, e_owner, origin_info) do { \
    esp_err_t _esp_err = (esp_err_expr); \
    if (_esp_err != ESP_OK) { \
        return STA_E(_esp_err, (e_owner), (origin_info)); \
    } \
} while(0)


/**
 * @brief Macro to push a status_rep_t to the buffer
 * @param status The status_rep_t to push
 */
#define STA_P(status) do { \
    status_rep_t _sta_p = (status); \
    _sta_push_overwrite(&_sta_p); \
} while(0)

/**
 * @brief Macro to create a status_rep_t from an esp_err_t expression and push it to the buffer, then return it
 * @param esp_err_expr Expression that evaluates to an esp_err_t
 * @param e_owner Enum value representing the owner of the error (status_origin_e)
 * @param origin_info Additional info about the error origin 
 */
#define STA_RP(status) do { \
    status_rep_t _sta_rp = (status); \
    STA_P(_sta_rp); \
    return _sta_rp; \
} while(0)

/**
 * @brief Macro to convert esp_err_t to status_rep_t with specified owner and origin info
 * @param esp_err_expr Expression that evaluates to an esp_err_t
 * @param e_owner Enum value representing the owner of the error (status_origin_e)
 * @param origin_info Additional info about the error origin 
 */
#define STA_FROM_ESP(esp_err_expr, e_owner, origin_info) ({ \
    esp_err_t _esp_err = (esp_err_expr); \
    status_rep_t _mapped_sta = STA_OK; \
    if (_esp_err != ESP_OK) { \
        _mapped_sta = STA_E(_esp_err, (e_owner), (origin_info)); \
    } \
    _mapped_sta; \
})

void status_manager_init(RingbufHandle_t status_buffer);
void status_manager_connection_update(bool connected);

