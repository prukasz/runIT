#pragma once

/**
 * @file err_macros.h
 * @brief Every macro of the errors component: the tags (ERR_TAG, ERR_TAGS_DECLARE, every map's), the node size, the
 * registration of a module's map (ERR_MODULE_DEFINE), constructors (ERR_NEW, ERR_WRAP, ERR_DATA) and propagation
 * (ERR_TRY, ERR_CHECK*, ERR_KEEP_FIRST, ERR_ESP), and the types and functions only the macros use (err__*: never
 * called directly). Included by errors.h at its top - include errors.h, not this file. The macros only use the types
 * and functions of errors.h when they expand, so the order is free.
 */

#include <stdbool.h>
#include <stdint.h>
#include "sdkconfig.h"

/* ----------------------------------------------------------------------------
 * Tags
 *
 * Each module lists its tags once, in its own header, as an X-macro map under its number (MODULE_ID_DECLARE,
 * module_id.h, in the module's <module>_id.h, which this header includes):
 *
 *   MODULE_ID_DECLARE(MODULE_ID_I2C, 0x02)
 *   #define ERR_I2C_MAP(X, M)                                                  \
 *     X(M, ERR_I2C_NACK,    1, ERR_SEV_HIGH,   { uint8_t addr; uint8_t reg; }) \
 *     X(M, ERR_I2C_TIMEOUT, 2, ERR_SEV_MEDIUM, { uint8_t addr; })
 *   ERR_TAGS_DECLARE(MODULE_ID_I2C, ERR_I2C_MAP)
 *
 * X(M, tag, code, severity, { payload fields }): M is the module the macros pass in (every row starts with it, as a
 * command table's rows with P), the code 1..0xFFFF within the module; the payload is the last argument, so field lists
 * may contain commas. The severity is the tag's wherever it is raised (err_sev). ERR_TAGS_DECLARE makes the tag enum
 * (tag = ERR_TAG(module, code)) and the packed err_data_<TAG>_t types. Then, in the module's main .c file:
 *
 *   ERR_MODULE_DEFINE(MODULE_ID_I2C, ERR_I2C_MAP)
 *
 * which builds the module's tag table (name, size, severity per tag) and registers it before app_main; the module's
 * name in the log is its MODULE_ID_ name without the prefix. The errors component never includes module headers - it
 * only looks tags up in registered tables. Put ERR_MODULE_DEFINE in a .c file the firmware uses anyway: an object
 * file nothing references is dropped by the linker, and its registration with it. Tag 0 means "no tag".
 * ------------------------------------------------------------------------- */

/**
 * @brief Error tag: high 16 bits = code module, low 16 bits = error code within
 * that module. Compared and switched on as a whole; split only when the module
 * itself is needed (logging, dispatch).
 * @usage ERR_TAG(MODULE_ID_I2C, 2)  // what ERR_TAGS_DECLARE gives ERR_I2C_TIMEOUT
 */
#define ERR_TAG(module, code) (((uint32_t)(module) << 16) | (uint16_t)(code))

// Parameter names end with _ so they never replace the .tag / .sev designators.
#define ERR__X_ENUM(mod_, tag_, code_, sev_, ...) tag_ = ERR_TAG(mod_, code_),
#define ERR__X_TYPE(mod_, tag_, code_, sev_, ...) typedef struct __attribute__((packed)) __VA_ARGS__ err_data_##tag_##_t;

/**
 * @brief Declares the tags (enum) and payload types of module `mod` (its MODULE_ID_<NAME>), map MAP(X, M). Use in its
 * header, after it includes the module's <module>_id.h.
 */
#define ERR_TAGS_DECLARE(mod, MAP) \
  enum { MAP(ERR__X_ENUM, mod) };  \
  MAP(ERR__X_TYPE, mod)

/* ----------------------------------------------------------------------------
 * Node size
 * ------------------------------------------------------------------------- */

// Node header: [info*] next* tag, no padding (checked in errors.h). On the board a pointer is 4 bytes (12 / 8 here);
// a 64-bit host build of the tests (host/) has 8-byte pointers.
#if CONFIG_ERRORS_INFO
#define ERR_HDR_LEN (2u * sizeof(void*) + 4u)
#else
#define ERR_HDR_LEN (sizeof(void*) + 4u)
#endif

/**
 * @brief Largest payload (`data`) that fits in one node (one pool block).
 */
#define ERR_DATA_MAX (CONFIG_ERRORS_POOL_BLOCK_SIZE - ERR_HDR_LEN)

/* ----------------------------------------------------------------------------
 * Registration
 * ------------------------------------------------------------------------- */

/** @brief One tag of a module's table (ERR_MODULE_DEFINE); looked up by err_tag_info (err_internal.h). */
typedef struct {
  uint32_t tag;
  const char* name;  // tag name as written in the map, e.g. "ERR_I2C_NACK"
  uint8_t size;      // sizeof(err_data_<TAG>_t)
  uint8_t sev;       // err_sev_e
} err_tag_info_t;

/** @brief A module's tag table (ERR_MODULE_DEFINE). */
typedef struct {
  uint16_t id;
  const char* name;
  const err_tag_info_t* tags;
  uint16_t count;
} err_module_t;

/** @brief Adds a module's table to the registry. Called by ERR_MODULE_DEFINE only. */
void err__register_module(const err_module_t* module);

// Parameter names end with _ so they never replace the .tag / .sev designators.
#define ERR__X_CHECK(mod_, tag_, code_, sev_, ...)                                                       \
  _Static_assert((code_) >= 1 && (code_) <= 0xFFFF, #tag_ ": the code is not 1..0xFFFF");                \
  _Static_assert(sizeof(err_data_##tag_##_t) <= ERR_DATA_MAX, #tag_ " data does not fit an error node");
#define ERR__X_CASE(mod_, tag_, code_, sev_, ...) case tag_:
#define ERR__X_INFO(mod_, tag_, code_, sev_, ...) \
  {.tag = tag_, .name = #tag_, .size = sizeof(err_data_##tag_##_t), .sev = sev_},

/**
 * @brief Defines and registers the tag table of module `mod` (its MODULE_ID_<NAME>), map MAP. Use once, in a .c file.
 * Fails to compile on a code out of 1..0xFFFF, a payload that does not fit a node, or two tags with one code
 * (duplicate case). At registration asserts that the module ID is not taken. The module's name (the log) is `mod`
 * without its MODULE_ID_ prefix.
 */
#define ERR_MODULE_DEFINE(mod, MAP)                                                                       \
  _Static_assert(sizeof(#mod) > sizeof("MODULE_ID_"), #mod ": not a MODULE_ID_<NAME>");                 \
  MAP(ERR__X_CHECK, mod)                                                                                \
  __attribute__((unused)) static void _err_dup_check_##mod(uint32_t t) {                                \
    switch (t) {                                                                                        \
      MAP(ERR__X_CASE, mod)                                                                             \
      default:                                                                                          \
        break;                                                                                          \
    }                                                                                                   \
  }                                                                                                     \
  static const err_tag_info_t _err_tags_##mod[] = {MAP(ERR__X_INFO, mod)};                              \
  static const err_module_t _err_module_##mod = {                                                       \
      .id = (mod),                                                                                      \
      .name = #mod + sizeof("MODULE_ID_") - 1u,                                                         \
      .tags = _err_tags_##mod,                                                                          \
      .count = sizeof(_err_tags_##mod) / sizeof((_err_tags_##mod)[0])};                                 \
  __attribute__((constructor)) static void _err_register_##mod(void) {                                  \
    err__register_module(&_err_module_##mod);                                                           \
  }

/* ----------------------------------------------------------------------------
 * Constructors
 *
 * Every tag has a payload type named err_data_<TAG>_t (packed - it goes on the
 * wire as is; an empty struct for tags without data). The macros fill it from
 * designated initializers; ERR_MODULE_DEFINE checks that it fits a node.
 *
 * Never return NULL for a failure: NULL means success. When the pool is empty:
 * ERR_NEW returns a static ERR_POOL_EMPTY node, ERR_WRAP returns `cause`
 * unchanged (the deeper error is kept, only this level is lost).
 * ------------------------------------------------------------------------- */

#if CONFIG_ERRORS_INFO_FILE
#define ERR__INFO_FILE .file = __FILE_NAME__,
#else
#define ERR__INFO_FILE
#endif
#if CONFIG_ERRORS_INFO_FUNC
#define ERR__INFO_FUNC .func = __func__,
#else
#define ERR__INFO_FUNC
#endif
#if CONFIG_ERRORS_INFO_LINE
#define ERR__INFO_LINE .line = __LINE__,
#else
#define ERR__INFO_LINE
#endif

#if CONFIG_ERRORS_INFO
/**
 * @brief Where a node was made: ERR__NEW builds one per call site (static, in flash) and the node only points to it;
 * err_log prints it. Only the fields enabled in Kconfig (CONFIG_ERRORS_INFO_FILE/FUNC/LINE) exist; with all three
 * off there is no err_info_t and no `info` in a node. Here, not in errors.h, because only the macros use it: it is
 * complete where ERR_NEW is called, never read outside the errors component.
 */
typedef struct err_info {
#if CONFIG_ERRORS_INFO_FILE
  const char* file;  // __FILE_NAME__
#endif
#if CONFIG_ERRORS_INFO_FUNC
  const char* func;  // __func__
#endif
#if CONFIG_ERRORS_INFO_LINE
  uint32_t line;  // __LINE__
#endif
} err_info_t;
#endif

struct err;
/**
 * @brief Creates a node. Called by ERR_NEW / ERR_WRAP (ERR__NEW) and the record parser only.
 * @param info  Call-site info (static, see ERR__NEW).
 * @param tag   ERR_TAG(module, code).
 * @param data  Payload, copied into the node. May be NULL when len == 0.
 * @param len   Payload length, at most ERR_DATA_MAX (longer is cut).
 * @param cause Deeper error; ownership passes to the new node. NULL for none.
 */
#if CONFIG_ERRORS_INFO
__attribute__((warn_unused_result)) struct err* err__new(const struct err_info* info, uint32_t tag, const void* data,
                                                         uint32_t len, struct err* cause);
#else
__attribute__((warn_unused_result)) struct err* err__new(uint32_t tag, const void* data, uint32_t len,
                                                         struct err* cause);
#endif

// Storage class in a compound literal is C2y: gcc accepts it in gnu23, clang (clangd) does not.
#ifdef __clang__
#define ERR__INFO_STORAGE
#else
#define ERR__INFO_STORAGE static
#endif
#if CONFIG_ERRORS_INFO
#define ERR__NEW(tag, data, len, cause) \
  err__new(&(ERR__INFO_STORAGE const err_info_t){ERR__INFO_FILE ERR__INFO_FUNC ERR__INFO_LINE}, tag, data, len, cause)
#else
#define ERR__NEW(tag, data, len, cause) err__new((tag), (data), (len), (cause))
#endif

/**
 * @brief New error wrapping `cause` (takes ownership of it).
 * @usage return ERR_WRAP(err, ERR_TPS_RAIL, .rail = 1, .want_mv = mv);
 */
#define ERR_WRAP(cause, tag, ...)                              \
  ({                                                           \
    const err_data_##tag##_t _err_data_ = {__VA_ARGS__};       \
    ERR__NEW(tag, &_err_data_, sizeof(_err_data_), cause);     \
  })

/**
 * @brief New root error (no cause).
 * @usage return ERR_NEW(ERR_I2C_NACK, .addr = addr, .reg = reg);
 */
#define ERR_NEW(tag, ...) ERR_WRAP(NULL, tag, __VA_ARGS__)

/**
 * @brief Typed view of a node's payload (err_data). The caller checks the node's tag first (err_tag, err_find).
 * @usage if (err_tag(e) == ERR_I2C_NACK) { uint8_t a = ERR_DATA(e, ERR_I2C_NACK)->addr; }
 */
#define ERR_DATA(e, tag) ((const err_data_##tag##_t*)err_data(e))

/* ----------------------------------------------------------------------------
 * Propagation
 *
 * For functions that return err_h. Each macro evaluates `call` once.
 * ------------------------------------------------------------------------- */

/**
 * @brief Returns the error of `call` from the current function, if any.
 * @usage ERR_TRY(i2c_read(dev, reg, &val));
 */
#define ERR_TRY(call)              \
  do {                             \
    err_h _err_try_ = call;        \
    if (_err_try_ != NULL) {       \
      return _err_try_;            \
    }                              \
  } while (0)

/**
 * @brief Returns a new error from the current function when `cond` is false.
 * @usage ERR_CHECK(ch < 8, ERR_ADC_BAD_CHANNEL, .ch = ch);
 */
#define ERR_CHECK(cond, tag, ...)       \
  do {                                  \
    if (!(cond)) {                      \
      return ERR_NEW(tag, __VA_ARGS__); \
    }                                   \
  } while (0)


static inline bool err__in_range_i(int64_t v, int64_t lo, int64_t hi) {
  return v >= lo && v <= hi;
}
static inline bool err__in_range_f(double v, double lo, double hi) {
  return v >= lo && v <= hi;  // NaN: false
}
/**
 * @brief Whether lo <= v <= hi: compared in int64_t, in double for a float or double v (NaN is never in range).
 * @usage if (!err_in_range(ms, 1, 1000)) { ... }
 */
#define err_in_range(v, lo, hi) \
  _Generic((v), float: err__in_range_f, double: err__in_range_f, default: err__in_range_i)(v, lo, hi)

#define ERR__RANGE_I(v, lo, hi) ERR_NEW(ERR_RANGE, .value = (int32_t)(v), .min = (int32_t)(lo), .max = (int32_t)(hi))
#define ERR__RANGE_F(v, lo, hi) ERR_NEW(ERR_RANGE_F, .value = (float)(v), .min = (float)(lo), .max = (float)(hi))
/**
 * @brief Returns ERR_RANGE {value, min, max} from the current function unless lo <= v <= hi; ERR_RANGE_F for a float
 * or double v. The check is exact (err_in_range); the payload is int32_t / float, so a value past them is reported cut.
 * v is evaluated once, lo and hi twice.
 * @usage ERR_CHECK_RANGE(args->duty, 0, IO_PWM_DUTY_MAX);
 */
#define ERR_CHECK_RANGE(v, lo, hi)                                                                         \
  do {                                                                                                     \
    const __typeof__(v) _err_v_ = (v);                                                                     \
    if (!err_in_range(_err_v_, lo, hi)) {                                                                  \
      return _Generic(_err_v_, float: ERR__RANGE_F(_err_v_, lo, hi), double: ERR__RANGE_F(_err_v_, lo, hi), \
                      default: ERR__RANGE_I(_err_v_, lo, hi));                                             \
    }                                                                                                      \
  } while (0)

/**
 * @brief Returns a new error from the current function unless lo <= v <= hi (err_in_range: no -Wtype-limits warning for
 * an unsigned v and a lo of 0).
 * @usage ERR_CHECK_IN_RANGE(ms, TIMEOUT_MIN_MS, TIMEOUT_MAX_MS, ERR_CMD_BAD_ARG);
 */
#define ERR_CHECK_IN_RANGE(v, lo, hi, tag, ...) ERR_CHECK(err_in_range(v, lo, hi), tag, __VA_ARGS__)

/**
 * @brief Keeps the first error of a sequence whose steps all run (teardown); a later error is freed.
 * @usage err_h err = NULL; ERR_KEEP_FIRST(err, chip_sleep(c, true)); ERR_KEEP_FIRST(err, ...);
 */
#define ERR_KEEP_FIRST(acc, call) \
  do {                            \
    err_h _err_keep_ = call;      \
    if ((acc) == NULL) {          \
      (acc) = _err_keep_;         \
    } else {                      \
      err_free(_err_keep_);       \
    }                             \
  } while (0)

/**
 * @brief Converts an ESP-IDF call result: NULL on ESP_OK, otherwise a new
 * ERR_ESP_ERR node with the esp_err_t code.
 * @usage err_h e = ERR_ESP(gpio_set_level(pin, 1));
 */
#define ERR_ESP(esp_call)                                                       \
  ({                                                                            \
    esp_err_t _err_esp_rc_ = esp_call;                                          \
    _err_esp_rc_ == ESP_OK ? NULL : ERR_NEW(ERR_ESP_ERR, .code = _err_esp_rc_); \
  })

/**
 * @brief ERR_TRY for an ESP-IDF call.
 * @usage ERR_TRY_ESP(i2c_master_transmit(h, buf, len, MSEC(10)));
 */
#define ERR_TRY_ESP(esp_call) ERR_TRY(ERR_ESP(esp_call))
