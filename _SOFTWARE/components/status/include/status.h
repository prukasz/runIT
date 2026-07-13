#pragma once
#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/ringbuf.h>
#include <freertos/task.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "freertos/queue.h"
#include "status_codes.h"

#define STATUS_INFO 0
#define STATUS_WARNING 1
#define STATUS_CRITICAL 2
#define STATUS_PAYLOAD_UNKNOWN 0
#define STATUS_PAYLOAD_SYS_IO 2
#define STATUS_PAYLOAD_ESP_ERR 3
#define STATUS_PAYLOAD_VM 4

/**
 * @brief Remote error / status report struct
 * @param e_code: UINT32_t Error code (status_err_code_e)
 * @param e_owner: UINT32_t Enum for error origin (status_owner_e)
 * @param track: Union for error tracking info (e.g. for for more info about owner)
 * @param details: (bitfield) severity (0-Info, 1-Warning, 2-Critical)
 */
typedef struct {
  uint32_t e_code;
  uint32_t e_owner;
  uint64_t payload;
  struct {
    uint8_t severity : 2;
    uint8_t payload_type : 6;
  } details;
} status_rep_t;

void _sta_push(const status_rep_t* item);

#define _STA_X(_code, _owner, _payload, _severity, _payload_type)                                                                         \
  (status_rep_t) {                                                                                                                        \
    .e_code = (_code), .e_owner = (_owner), .payload = (_payload), .details = {.severity = (_severity), .payload_type = (_payload_type) } \
  }

#define STA_OK ((status_rep_t){0})

#define STA_C(e_code, e_owner, payload, payload_type) _STA_X((e_code), (e_owner), (payload), STATUS_CRITICAL, (payload_type))
#define STA_W(e_code, e_owner, payload, payload_type) _STA_X((e_code), (e_owner), (payload), STATUS_WARNING, (payload_type))
#define STA_I(e_code, e_owner, payload, payload_type) _STA_X((e_code), (e_owner), (payload), STATUS_INFO, (payload_type))

#define STA_IS_OK(err) (((err).e_code == 0))
#define STA_IS_ERR(err) (((err).e_code != 0) && ((err).details.severity > STATUS_INFO))

#define STA_P(status)               \
  do {                              \
    status_rep_t _sta_p = (status); \
    _sta_push(&_sta_p);             \
  } while (0)

#define STA_RP(status)               \
  do {                               \
    status_rep_t _sta_rp = (status); \
    if (STA_IS_ERR(_sta_rp)) {       \
      STA_P(_sta_rp);                \
    }                                \
    return _sta_rp;                  \
  } while (0)

#define _STA_EMIT(sta, R, P)        \
  do {                              \
    status_rep_t _sta_emit = (sta); \
    if (P) {                        \
      STA_P(_sta_emit);             \
    }                               \
    if (R) {                        \
      return _sta_emit;             \
    }                               \
  } while (0)

#define STA_X_ON_ERR(status, R, P)  \
  do {                              \
    status_rep_t _sta_x = (status); \
    if (_sta_x.e_code != 0) {       \
      _STA_EMIT(_sta_x, R, P);      \
    }                               \
  } while (0)

#define STA_R_ON_ERR(status) STA_X_ON_ERR(status, 1, 0)
#define STA_P_ON_ERR(status) STA_X_ON_ERR(status, 0, 1)
#define STA_RP_ON_ERR(status) STA_X_ON_ERR(status, 1, 1)

/* --- Handle / null-pointer checks (R, P configurable) --- */

#define CHECK_HANDLE_X(handle, R, P, payload, payload_type)                                \
  do {                                                                                     \
    if ((handle) == NULL) {                                                                \
      ESP_LOGE(__FILE_NAME__, "%s: No handle for '%s'", __func__, #handle);                \
      status_rep_t _sta_err = STA_C(ERR_MISSING_HANDLE, OWNER, (payload), (payload_type)); \
      _STA_EMIT(_sta_err, R, P);                                                           \
    }                                                                                      \
  } while (0)
#define CHECK_HANDLE_R(handle) CHECK_HANDLE_X(handle, 1, 0, 0, STATUS_PAYLOAD_UNKNOWN)
#define CHECK_HANDLE_RP(handle) CHECK_HANDLE_X(handle, 1, 1, 0, STATUS_PAYLOAD_UNKNOWN)
#define CHECK_HANDLE_P(handle) CHECK_HANDLE_X(handle, 0, 1, 0, STATUS_PAYLOAD_UNKNOWN)
#define CHECK_NOT_NULL_X(ptr, R, P, payload, payload_type)                       \
  do {                                                                           \
    if ((ptr) == NULL) {                                                         \
      ESP_LOGE(__FILE_NAME__, "%s: Pointer '%s' is NULL", __func__, #ptr);       \
      _STA_EMIT(STA_C(ERR_INVALID_ARG, OWNER, (payload), (payload_type)), R, P); \
    }                                                                            \
  } while (0)

#define CHECK_NOT_NULL_R(ptr) CHECK_NOT_NULL_X(ptr, 1, 0, 0, STATUS_PAYLOAD_UNKNOWN)
#define CHECK_NOT_NULL_RP(ptr) CHECK_NOT_NULL_X(ptr, 1, 1, 0, STATUS_PAYLOAD_UNKNOWN)

/* --- Argument range checks --- */

#define CHECK_ARG_X(arg, min_val, max_val, override_return, R, P)                                                                                    \
  do {                                                                                                                                               \
    __typeof__(arg) _a = (arg);                                                                                                                      \
    __typeof__(min_val) _min = (min_val);                                                                                                            \
    __typeof__(max_val) _max = (max_val);                                                                                                            \
    if (_a < _min || _a > _max) {                                                                                                                    \
      ESP_LOGE(__FILE_NAME__, "%s: Argument '%s' out of range [%lld, %lld] (Got: %lld)", __func__, #arg, (int64_t)_min, (int64_t)_max, (int64_t)_a); \
      _STA_EMIT(STA_C(ERR_INVALID_ARG, OWNER, (override_return) ? (int64_t)(override_return) : (int64_t)_a, STATUS_PAYLOAD_UNKNOWN), R, P);          \
    }                                                                                                                                                \
  } while (0)

#define CHECK_ARG_R(arg, min_val, max_val, override_return) CHECK_ARG_X(arg, min_val, max_val, override_return, 1, 0)
#define CHECK_ARG_RP(arg, min_val, max_val, override_return) CHECK_ARG_X(arg, min_val, max_val, override_return, 1, 1)

/* --- ESP-IDF call checks --- */
#define STA_FROM_ESP(esp_err_expr)                                           \
  ({                                                                         \
    esp_err_t _esp_err = (esp_err_expr);                                     \
    status_rep_t _mapped_sta = STA_OK;                                       \
    if (_esp_err != ESP_OK) {                                                \
      _mapped_sta = STA_C(ERR_ESP, OWNER, _esp_err, STATUS_PAYLOAD_ESP_ERR); \
    }                                                                        \
    _mapped_sta;                                                             \
  })

#define CHECK_ESP_CALL_X(esp_err_call, R, P)                                                                                 \
  do {                                                                                                                       \
    esp_err_t _err = (esp_err_call);                                                                                         \
    if (_err != ESP_OK) {                                                                                                    \
      ESP_LOGE(__FILE_NAME__, "%s: ESP API Failed '%s' -> %s (0x%x)", __func__, #esp_err_call, esp_err_to_name(_err), _err); \
      status_rep_t _sta_err = STA_C(ERR_ESP, OWNER, _err, STATUS_PAYLOAD_ESP_ERR);                                           \
      _STA_EMIT(_sta_err, R, P);                                                                                             \
    }                                                                                                                        \
  } while (0)

#define CHECK_ESP_CALL_R(esp_err_call) CHECK_ESP_CALL_X(esp_err_call, 1, 0)
#define CHECK_ESP_CALL_P(esp_err_call) CHECK_ESP_CALL_X(esp_err_call, 0, 1)
#define CHECK_ESP_CALL_RP(esp_err_call) CHECK_ESP_CALL_X(esp_err_call, 1, 1)

/**
 * clamp to min max
 */
#define CLAMP(val, min_val, max_val)                                                      \
  ({                                                                                      \
    __typeof__(val) _val = (val);                                                         \
    __typeof__(min_val) _min = (min_val);                                                 \
    __typeof__(max_val) _max = (max_val);                                                 \
    (_val < _min) ? (__typeof__(val))_min : (_val > _max) ? (__typeof__(val))_max : _val; \
  })

// Backwards-compatible helper macros:

void status_assign_error_tx(uint16_t char_uuid, uint8_t buffer_id, QueueHandle_t status_queue);
void status_set_rep_mode(uint8_t rep_level);
void status_suspend(void);
void status_resume(void);