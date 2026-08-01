#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_log.h"
#include "sys_error_codes.h"

// ---------------------------------------------------------
// 1. Tags and Payload Structures (Auto-generated)
// ---------------------------------------------------------

// Auto-generate the enum for the tags
#define X_ENUM(tag, struct_def) tag,
typedef enum { SYS_ERROR_MAP(X_ENUM) ERR_MAX_COUNT } err_tag_e;
#undef X_ENUM

// Auto-generate the payload structs
#define X_STRUCT(tag, struct_def) typedef struct_def err_payload_##tag##_t;
SYS_ERROR_MAP(X_STRUCT)
#undef X_STRUCT

// ---------------------------------------------------------
// 2. Main Error Structure (Linked List Node)
// ---------------------------------------------------------

typedef struct err_node {
  err_tag_e tag;
  uint32_t owner;

  struct err_node* next_cause;

  // Flexible array member: payload data is stored sequentially after the struct
  uint8_t payload[] __attribute__((aligned(8)));
} sys_err_t;

typedef sys_err_t* err_h;

// ---------------------------------------------------------
// 3. Ring Buffer Allocator API
// ---------------------------------------------------------

void SE_init(void);
err_h SE_alloc_bytes(size_t payload_size, err_tag_e tag, uint32_t owner);

// Pushes the final error chain to the error handler task/queue
void SE_push_to_handler(err_h err);

/**
 * @brief Hook the error handler task calls for the first device-owned node
 * found in a chain it's about to print/send (see sys_error_handler.c) - lets
 * sys_device dispatch to that device's own error handling without sys_errors
 * depending on sys_device (which already depends on sys_errors, so the
 * reverse would be circular). Registered once via
 * SE_register_device_error_hook(), typically from a load-time constructor
 * per the [[runit]] skill's static-construction convention.
 */
typedef err_h (*se_device_error_hook_t)(uint8_t device_id, err_h error);
void SE_register_device_error_hook(se_device_error_hook_t hook);

// Suspend/Resume error processing
void SE_suspend(void);
void SE_resume(void);
bool SE_is_suspended(void);

// Name lookup helper functions
const char* SE_get_owner_name(uint32_t owner);
const char* SE_get_tag_name(err_tag_e tag);

/**
 * @brief Size of the payload struct generated for @p tag.
 *
 * A node does not store its own payload length — it is a property of the tag.
 * Used by the error encoder to know how many bytes after `sys_err_t` belong to
 * the node, and to put that length on the wire for the client.
 *
 * @param tag Error tag.
 * @return size_t `sizeof(err_payload_<tag>_t)`, or 0 for an unknown tag.
 */
size_t SE_get_payload_size(err_tag_e tag);

// ---------------------------------------------------------
// 3b. Logging / Telemetry Configuration
// ---------------------------------------------------------

/** @brief Stack buffer the log hook renders one line into; a longer line is truncated on BLE only (serial still gets it whole). */
#define SE_LOG_LINE_MAX 256

/** @brief Compile-time ceiling for sys_error_cfg_t.errors.packet_max (static encode buffer). */
#define SE_ERR_PACKET_MAX 244

/**
 * @brief Where logs and error chains go.
 *
 * Deliberately flat, and deliberately without per-sink level fields: level
 * selection is `esp_log`'s job — `global_level` plus any per-tag
 * `esp_log_level_set()` the application makes — so a line that reaches the hook
 * is a line every enabled sink wants. What is left is a set of routing
 * switches small enough to be applied remotely as one packet payload, in the
 * style of the decoder structs in [[CODECS.MD]].
 */
typedef struct {
  /** @brief Applied verbatim as `esp_log_level_set("*", global_level)` — the only level filter in the path. */
  esp_log_level_t global_level;

  struct {
    bool mirror_on_serial;  /**< Keep writing lines to the original sink (UART/stdout). Clear it to make BLE the only log sink. */
    bool ble_enable;        /**< Also push each rendered line to a BLE characteristic, one line per notification. */
    uint16_t char_uuid;     /**< Target characteristic (required when ble_enable). */
    uint8_t tx_header;      /**< TX slot header byte identifying the log stream (see sys_ble_char_assign_tx_buffer()). */
  } logs;

  struct {
    bool serial_trace;    /**< Print the decoded owner/tag stack trace with ESP_LOGE. */
    bool ble_enable;      /**< Encode each chain with enc_sys_errors_encode_chain() and push it to a BLE characteristic. */
    uint16_t char_uuid;   /**< Target characteristic (required when ble_enable). */
    uint8_t tx_header;    /**< TX slot header byte identifying the error stream. */
    uint16_t packet_max;  /**< Largest encoded packet; clamped to SE_ERR_PACKET_MAX. Longer chains are truncated, never split. */
  } errors;
} sys_error_cfg_t;

/**
 * @brief Boot defaults: serial only, nothing on BLE.
 *
 * These are the settings in force before the first SE_configure() call, so a
 * board that never calls it behaves exactly as it did before telemetry existed.
 */
#define SYS_ERROR_CFG_DEFAULT()                                                                                        \
  ((sys_error_cfg_t){                                                                                                  \
      .global_level = ESP_LOG_INFO,                                                                                    \
      .logs = {.mirror_on_serial = true, .ble_enable = false, .char_uuid = 0, .tx_header = 0},                         \
      .errors = {.serial_trace = true, .ble_enable = false, .char_uuid = 0, .tx_header = 0, .packet_max = SE_ERR_PACKET_MAX}})

/**
 * @brief Apply a logging/telemetry configuration.
 *
 * Idempotent and callable at any time after SE_init(); every call re-points
 * the `esp_log` vprintf hook to `se_log_vprintf` (a no-op past the first, same
 * function each time), so it only ever needs to swap the routing switches.
 * Note that clearing both `logs.mirror_on_serial` and `logs.ble_enable`
 * silences log output entirely — the hook stays installed and simply drops
 * every line.
 *
 * @param cfg Configuration to apply.
 * @return err_h NULL on success, ERR_NULL_PTR for a NULL @p cfg, or
 *               ERR_INVALID_VAL_UI32 if a stream is enabled without a
 *               characteristic UUID.
 *
 * Example:
 * @code
 * SE_ORIGIN_CALL(SE_configure(&(sys_error_cfg_t){
 *     .global_level = ESP_LOG_INFO,
 *     .logs = {.mirror_on_serial = true, .ble_enable = true,
 *              .char_uuid = SYS_BLE_CHR_RUNIT_LOGS, .tx_header = PACKET_HEADER_LOGS},
 *     .errors = {.serial_trace = true, .ble_enable = true,
 *                .char_uuid = SYS_BLE_CHR_RUNIT_LOGS, .tx_header = PACKET_HEADER_ERRORS,
 *                .packet_max = SE_ERR_PACKET_MAX},
 * }));
 *
 * // Per-tag filtering stays where it belongs - in esp_log itself:
 * esp_log_level_set("sys_i2c", ESP_LOG_DEBUG);
 * @endcode
 */
err_h SE_configure(const sys_error_cfg_t* cfg);

/**
 * @brief Read back the configuration currently in force (post-clamping).
 *
 * @param out_cfg Destination struct.
 * @return err_h NULL on success, or ERR_NULL_PTR.
 */
err_h SE_get_config(sys_error_cfg_t* out_cfg);

// ---------------------------------------------------------
// 4. Core Macros (Call-ready, implicitly use 'OWNER')
// ---------------------------------------------------------

#define SE_IS_OK(call) ((call) == NULL)
#define SE_IS_ERR(call) ((call) != NULL)

// Allocates and fills a payload for `tag_name`; leaves next_cause as NULL
#define SE_ERR_NEW(tag_name, ...)                                                                \
  ({                                                                                              \
    err_h __e = SE_alloc_bytes(sizeof(err_payload_##tag_name##_t), tag_name, OWNER);              \
    *((err_payload_##tag_name##_t*)__e->payload) = (err_payload_##tag_name##_t){__VA_ARGS__};     \
    __e;                                                                                          \
  })

#define SE_WRAP_ERR(rc_err, tag_name, ...)               \
  ({                                                     \
    err_h __new_err = SE_ERR_NEW(tag_name, __VA_ARGS__); \
    __new_err->next_cause = (rc_err);                    \
    __new_err;                                           \
  })

#define SE_WRAP_DEV_ERR(rc_err, dep_dev_id) SE_WRAP_ERR((rc_err), ERR_DEV_DEP_FAILED, .dev_id = (dep_dev_id))

// Executes a call, and if it fails, wraps the error and returns it
#define SE_PASS_ON_ERR(call, tag_name, ...)             \
  do {                                                   \
    err_h __rc_err = (call);                             \
    if (__rc_err != NULL) {                              \
      return SE_WRAP_ERR(__rc_err, tag_name, __VA_ARGS__); \
    }                                                    \
  } while (0)

// Macro to return an error of a specific tag with variable payload
#define SE_SET_ERR(out_err, tag_name, ...) ((out_err) = SE_ERR_NEW(tag_name, __VA_ARGS__))

// Macro to create and emit an error without returning it
#define SE_EMIT_ERR(tag_name, ...) SE_push_to_handler(SE_ERR_NEW(tag_name, __VA_ARGS__))

#define SE_RET_ERR(tag_name, ...) return SE_ERR_NEW(tag_name, __VA_ARGS__)

// ---------------------------------------------------------
// 5. Utility / Compatibility Macros
// ---------------------------------------------------------

#define SE_CONVERT_ESP(esp_call)                            \
  ({                                                        \
    esp_err_t __rc = (esp_call);                            \
    __rc != ESP_OK ? SE_ERR_NEW(ERR_ESP_ERR, .esp_code = __rc) : (err_h)NULL; \
  })

#define SE_RET_IF_ESP_ERR(esp_call)              \
  do {                                        \
    esp_err_t __rc = (esp_call);              \
    if (__rc != ESP_OK) {                     \
      SE_RET_ERR(ERR_ESP_ERR, .esp_code = __rc); \
    }                                         \
  } while (0)

// SE_push_to_handler() already no-ops while suspended
#define SE_ORIGIN_CALL(call)           \
  do {                              \
    err_h __err = (call);           \
    if (__err != NULL) {            \
      SE_push_to_handler(__err);    \
    }                               \
  } while (0)

#define SE_CHECK_NOT_NULL(ptr)     \
  do {                          \
    if ((ptr) == NULL) {        \
      SE_RET_ERR(ERR_NULL_PTR, 0); \
    }                           \
  } while (0)

#define SE_CHECK_IF_ALLOCATED(ptr)      \
  do {                               \
    if ((ptr) == NULL) {             \
      SE_RET_ERR(ERR_BASE_NO_MEM, 0);   \
    }                                \
  } while (0)

#define SE_CHECK_HANDLE(handle)     \
  do {                           \
    if ((handle) == NULL) {      \
      SE_RET_ERR(ERR_NO_HANDLE, 0); \
    }                            \
  } while (0)

// Parameters are named in_val/in_min/in_max (not val/min/max) so they can't
// collide, via plain token substitution, with the .val/.min/.max designators
// used to fill the payload below.
#define SE_CHECK_IN_RANGE_UI32(in_val, in_min, in_max)                                                    \
  do {                                                                                                     \
    uint64_t __v = (uint64_t)(in_val);                                                                    \
    uint64_t __mn = (uint64_t)(in_min);                                                                   \
    uint64_t __mx = (uint64_t)(in_max);                                                                   \
    if (__v < __mn || __v > __mx) {                                                                       \
      SE_RET_ERR(ERR_INVALID_VAL_UI32, .val = (uint32_t)__v, .min = (uint32_t)__mn, .max = (uint32_t)__mx); \
    }                                                                                                      \
  } while (0)

#define SE_CHECK_IN_RANGE_I32(in_val, in_min, in_max)                                                    \
  do {                                                                                                    \
    int64_t __v = (int64_t)(in_val);                                                                     \
    int64_t __mn = (int64_t)(in_min);                                                                     \
    int64_t __mx = (int64_t)(in_max);                                                                     \
    if (__v < __mn || __v > __mx) {                                                                       \
      SE_RET_ERR(ERR_INVALID_VAL_I32, .val = (int32_t)__v, .min = (int32_t)__mn, .max = (int32_t)__mx);    \
    }                                                                                                      \
  } while (0)

#define SE_CHECK_IN_RANGE_F(in_val, in_min, in_max)                     \
  do {                                                                  \
    float __v = (float)(in_val);                                       \
    float __mn = (float)(in_min);                                      \
    float __mx = (float)(in_max);                                      \
    if (__v < __mn || __v > __mx) {                                    \
      SE_RET_ERR(ERR_INVALID_VAL_F, .val = __v, .min = __mn, .max = __mx); \
    }                                                                   \
  } while (0)

// NOTE: min/max must share val's signedness — e.g. CHECK_IN_RANGE(some_int8, -1, 10)
// dispatches on the *value*'s type only, so an unsigned val with a negative bound
// will route to the UI32 branch and the negative bound will wrap to a huge uint.
#define SE_CHECK_IN_RANGE(val, min, max)                                                   \
  _Generic((val),                                                                       \
    float: ({ SE_CHECK_IN_RANGE_F((val), (min), (max)); }),                                \
    double: ({ SE_CHECK_IN_RANGE_F((val), (min), (max)); }),                               \
    int: ({ SE_CHECK_IN_RANGE_I32((val), (min), (max)); }),                                 \
    signed char: ({ SE_CHECK_IN_RANGE_I32((val), (min), (max)); }),                         \
    short: ({ SE_CHECK_IN_RANGE_I32((val), (min), (max)); }),                              \
    long: ({ SE_CHECK_IN_RANGE_I32((val), (min), (max)); }),                                \
    long long: ({ SE_CHECK_IN_RANGE_I32((val), (min), (max)); }),                           \
    default: ({ SE_CHECK_IN_RANGE_UI32((val), (min), (max)); })                            \
  )

#define SE_RET_IF_ERR(call) SE_PASS_ON_ERR((call), ERR_DEP_FAILED, 0)
