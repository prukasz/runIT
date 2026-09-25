#pragma once
#include <stdint.h>
#include <stdio.h>


struct err_node;
typedef struct err_node* err_h;

/* Every function returning err_h is marked with this: a dropped error chain
   leaks a pool slot and hides a failure. Intentional drops: SE_release(call). */
#define SE_MUST_USE __attribute__((warn_unused_result))

// Owners for the sys_errors component itself (config + telemetry plumbing).
#define SYS_ERRORS_OWNER_MAP(X)                             \
  X(OWNER_SYS_ERRORS_BASE, 0xA800, "OWNER_SYS_ERRORS_BASE") \
  X(OWNER_SYS_ERRORS_CONFIG, 0xA801, "OWNER_SYS_ERRORS_CONFIG")

/**
 * @brief Error severity classification matching device importance order.
 * Defined here so all submodule error maps can assign default severity levels.
 */
typedef enum se_level_e {
  SE_LEVEL_NONE = 0,
  SE_LEVEL_LOW = 1,
  SE_LEVEL_MEDIUM = 2,
  SE_LEVEL_HIGH = 3,
  SE_LEVEL_CRITICAL = 4,
} se_level_e;


#define SYS_ERROR_BASE_MAP(X)                                           \
  X(ERR_NO_HANDLE, 0x0001, SE_LEVEL_HIGH, struct { uint8_t unused; })        \
  X(ERR_NULL_PTR, 0x0002, SE_LEVEL_HIGH, struct { uint8_t unused; })         \
  X(ERR_INVALID_VAL_UI32, 0x0003, SE_LEVEL_LOW, struct {                     \
      uint32_t val;                                                     \
      uint32_t min;                                                     \
      uint32_t max;                                                     \
    })                                                                  \
  X(ERR_INVALID_VAL_I32, 0x0004, SE_LEVEL_LOW, struct {                      \
      int32_t val;                                                      \
      int32_t min;                                                      \
      int32_t max;                                                      \
    })                                                                  \
  X(ERR_INVALID_VAL_F, 0x0005, SE_LEVEL_LOW, struct {                        \
      float val;                                                        \
      float min;                                                        \
      float max;                                                        \
    })                                                                  \
  X(ERR_DEV_DEP_FAILED, 0x0006, SE_LEVEL_HIGH, struct { uint8_t dev_id; /*@id device*/ })   \
  X(ERR_DEP_FAILED, 0x0007, SE_LEVEL_HIGH, struct { uint8_t unused; })       \
  X(ERR_ESP_ERR, 0x0008, SE_LEVEL_HIGH, struct { esp_err_t esp_code; /*@id esp-err*/ })      \
  X(ERR_BASE_NO_MEM, 0x0009, SE_LEVEL_HIGH, struct { uint8_t unused; })      \
  X(ERR_BASE_NOT_SUPPORTED, 0x000A, SE_LEVEL_LOW, struct { uint8_t unused; })\
  X(ERR_BASE_NOT_FOUND, 0x000B, SE_LEVEL_LOW, struct { uint8_t unused; })   \
  X(ERR_BASE_INVALID_STATE, 0x000C, SE_LEVEL_MEDIUM, struct { uint8_t unused; }) \
  X(ERR_BASE_POOL_EXHAUSTED, 0x000D, SE_LEVEL_LOW, struct { uint8_t unused; })

/**
 * @brief Human-readable descriptions for base tags - see SE_describe_payload() in sys_error.h.
 */
#define SYS_ERROR_BASE_LOGGER_MAP(X) \
  X(ERR_NO_HANDLE)                   \
  X(ERR_NULL_PTR)                    \
  X(ERR_INVALID_VAL_UI32)            \
  X(ERR_INVALID_VAL_I32)             \
  X(ERR_INVALID_VAL_F)               \
  X(ERR_DEV_DEP_FAILED)              \
  X(ERR_DEP_FAILED)                  \
  X(ERR_ESP_ERR)                     \
  X(ERR_BASE_NO_MEM)                 \
  X(ERR_BASE_NOT_SUPPORTED)          \
  X(ERR_BASE_NOT_FOUND)              \
  X(ERR_BASE_INVALID_STATE)          \
  X(ERR_BASE_POOL_EXHAUSTED)

#define LOG_BODY_ERR_NO_HANDLE(p, out, out_size) \
  do {                                           \
    (void)(p);                                   \
    snprintf((out), (out_size), "no handle");    \
  } while (0)
#define LOG_BODY_ERR_NULL_PTR(p, out, out_size)             \
  do {                                                      \
    (void)(p);                                              \
    snprintf((out), (out_size), "unexpected NULL pointer"); \
  } while (0)
#define LOG_BODY_ERR_INVALID_VAL_UI32(p, out, out_size) snprintf((out), (out_size), "value %lu out of range [%lu, %lu]", (unsigned long)(p)->val, (unsigned long)(p)->min, (unsigned long)(p)->max)
#define LOG_BODY_ERR_INVALID_VAL_I32(p, out, out_size)  snprintf((out), (out_size), "value %ld out of range [%ld, %ld]", (long)(p)->val, (long)(p)->min, (long)(p)->max)
#define LOG_BODY_ERR_INVALID_VAL_F(p, out, out_size)    snprintf((out), (out_size), "value %g out of range [%g, %g]", (double)(p)->val, (double)(p)->min, (double)(p)->max)
#define LOG_BODY_ERR_DEV_DEP_FAILED(p, out, out_size)   snprintf((out), (out_size), "device %u dependency failed", (p)->dev_id)
#define LOG_BODY_ERR_DEP_FAILED(p, out, out_size)      \
  do {                                                 \
    (void)(p);                                         \
    snprintf((out), (out_size), "nested call failed"); \
  } while (0)
#define LOG_BODY_ERR_ESP_ERR(p, out, out_size) snprintf((out), (out_size), "ESP-IDF error %s (0x%x)", esp_err_to_name((p)->esp_code), (p)->esp_code)
#define LOG_BODY_ERR_BASE_NO_MEM(p, out, out_size) \
  do {                                             \
    (void)(p);                                     \
    snprintf((out), (out_size), "out of memory");  \
  } while (0)
#define LOG_BODY_ERR_BASE_NOT_SUPPORTED(p, out, out_size)   \
  do {                                                      \
    (void)(p);                                              \
    snprintf((out), (out_size), "operation not supported"); \
  } while (0)
#define LOG_BODY_ERR_BASE_NOT_FOUND(p, out, out_size) \
  do {                                                \
    (void)(p);                                        \
    snprintf((out), (out_size), "not found");         \
  } while (0)
#define LOG_BODY_ERR_BASE_INVALID_STATE(p, out, out_size)            \
  do {                                                               \
    (void)(p);                                                       \
    snprintf((out), (out_size), "invalid state for this operation"); \
  } while (0)
#define LOG_BODY_ERR_BASE_POOL_EXHAUSTED(p, out, out_size)                                     \
  do {                                                                                        \
    (void)(p);                                                                                \
    snprintf((out), (out_size), "error pool exhausted: this error's details were not stored"); \
  } while (0)
