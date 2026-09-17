#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "esp_err.h"
#include "esp_log.h"
#include "sys_error_codes.h"

// ---------------------------------------------------------
// 1. Tags and Payload Structures (Auto-generated)
// ---------------------------------------------------------

// Auto-generate the enum for the tags
#define X_ENUM(tag, level, struct_def) tag,
typedef enum { SYS_ERROR_MAP(X_ENUM) ERR_MAX_COUNT } err_tag_e;
#undef X_ENUM

// Auto-generate the payload structs
#define X_STRUCT(tag, level, struct_def) typedef struct_def err_payload_##tag##_t;
SYS_ERROR_MAP(X_STRUCT)
#undef X_STRUCT

// Auto-generate one typed logger function per tag in SYS_ERROR_LOGGER_MAP
// (aggregated in sys_error_codes.h from every module's own opt-in
// SYS_ERROR_<MODULE>_LOGGER_MAP) - deferred to here, rather than living in
// each module's sys_error_<module>.h, because err_payload_<tag>_t doesn't
// exist until the SYS_ERROR_MAP(X_STRUCT) expansion just above. Each
// module only had to supply a LOG_BODY_<tag>(p, out, out_size) macro (pure
// text, no ordering constraint); this stamps it into a real function.
#define X_LOGGER_FN(tag) \
  static inline void log_##tag(const err_payload_##tag##_t* p, char* out, size_t out_size) { LOG_BODY_##tag(p, out, out_size); }
SYS_ERROR_LOGGER_MAP(X_LOGGER_FN)
#undef X_LOGGER_FN

/**
 * @brief Writes a short human-readable description of a node's payload into
 * out, if tag has a registered logger (see SYS_ERROR_LOGGER_MAP).
 *
 * @param tag Node's tag - determines which logger (if any) runs.
 * @param payload Node's payload (curr->payload) - cast to the matching
 *                err_payload_<tag>_t internally.
 * @param out Destination buffer; snprintf-truncated, always NUL-terminated
 *            if out_size > 0.
 * @param out_size Size of out.
 * @return bool true if a description was written, false if tag has no
 *              logger registered (out is left untouched) - callers should
 *              fall back to a raw payload dump in that case.
 */
static inline bool SE_describe_payload(err_tag_e tag, const void* payload, char* out, size_t out_size) {
  switch (tag) {
#define X_LOGGER_CASE(tag) \
  case tag:                \
    log_##tag((const err_payload_##tag##_t*)payload, out, out_size); \
    return true;
    SYS_ERROR_LOGGER_MAP(X_LOGGER_CASE)
#undef X_LOGGER_CASE
    default:
      return false;
  }
}

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
 * @brief Weak domain error hook for sys_errors faults.
 */
extern err_h sys_errors_report_fault(err_h node, err_h chain) __attribute__((weak));

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
sys_device_err_level_e SE_get_tag_level(err_tag_e tag);

/** @brief Maximum depth of an error chain traversal before assuming a cycle or overflow. */
#define SE_MAX_CHAIN_DEPTH 16u

/**
 * @brief Validates that an error handle points to a valid, 8-byte-aligned
 *        location within the error ring buffer.
 *
 * @param err Error pointer to validate.
 * @return bool true if valid and inside the ring buffer, false otherwise.
 */
bool SE_is_valid_error_ptr(err_h err);

/**
 * @brief Walks an error chain down its next_cause links to find the root cause.
 *
 * Traversal is capped at SE_MAX_CHAIN_DEPTH and verifies each next_cause
 * pointer with SE_is_valid_error_ptr() to protect against runaway chains,
 * cycles, and out-of-bounds reads caused by ring buffer wraps.
 *
 * @param error Outermost error handle.
 * @return err_h Deepest cause node where next_cause is NULL, or NULL if error is
 *              NULL, invalid, deeper than SE_MAX_CHAIN_DEPTH, or cyclic.
 */
err_h SE_get_error_root(err_h error);



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
