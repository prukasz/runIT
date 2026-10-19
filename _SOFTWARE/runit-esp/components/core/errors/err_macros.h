#pragma once

/**
 * @file err_macros.h
 * @brief Every macro of the errors component: tag helpers, node size, tag
 * registry (X-macros), constructors and propagation. Included by errors.h at
 * its top - include errors.h, not this file. The macros only use the types and
 * functions of errors.h when they expand, so the order is free.
 */

#include "sdkconfig.h"

/* ----------------------------------------------------------------------------
 * Tags and node size
 * ------------------------------------------------------------------------- */

/**
 * @brief Error tag: high 16 bits = code module, low 16 bits = error code within
 * that module. Compared and switched on as a whole; split only when the module
 * itself is needed (logging, dispatch).
 * @usage enum { ERR_I2C_NACK = ERR_TAG(MOD_I2C, 2) };
 */
#define ERR_TAG(module, code) ((((uint32_t)(uint16_t)(module)) << 16) | (uint16_t)(code))
#define ERR_MODULE(tag) ((uint16_t)((uint32_t)(tag) >> 16))
#define ERR_CODE(tag) ((uint16_t)(tag))

// Node header: [info*] next tag, no padding (checked in errors.h).
#if CONFIG_ERRORS_INFO
#define ERR_HDR_LEN 12u
#else
#define ERR_HDR_LEN 8u
#endif

/**
 * @brief Largest payload (`data`) that fits in one node (one pool block).
 */
#define ERR_DATA_MAX (CONFIG_ERRORS_POOL_BLOCK_SIZE - ERR_HDR_LEN)

/* ----------------------------------------------------------------------------
 * Tag registry
 *
 * Each module lists its tags once, in its own header, as an X-macro map:
 *
 *   #define ERR_I2C_MAP(X)                                                        \
 *     X(ERR_I2C_NACK,    ERR_TAG(ERR_MOD_I2C, 1), ERR_SEV_HIGH,   { uint8_t addr; uint8_t reg; }) \
 *     X(ERR_I2C_TIMEOUT, ERR_TAG(ERR_MOD_I2C, 2), ERR_SEV_MEDIUM, { uint8_t addr; })
 *   ERR_TAGS_DECLARE(ERR_I2C_MAP)
 *
 * X(tag, id, severity, { payload fields }) - the payload is the last argument,
 * so field lists may contain commas. The severity is the tag's wherever it is
 * raised (err_sev). ERR_TAGS_DECLARE makes the tag enum and the packed
 * err_data_<TAG>_t types. Then, in the module's main .c file:
 *
 *   ERR_MODULE_DEFINE(ERR_MOD_I2C, "i2c", ERR_I2C_MAP)
 *
 * which builds the module's tag table (name, size, severity per tag) and
 * registers it before app_main. The errors component never includes module
 * headers - it only looks tags up in registered tables. Put ERR_MODULE_DEFINE
 * in a .c file the firmware uses anyway: an object file nothing references is
 * dropped by the linker, and its registration with it.
 *
 * Module IDs (ERR_MOD_*) must be unique across the firmware - they are all
 * assigned in err_modules.h. Tag 0 means "no tag".
 * ------------------------------------------------------------------------- */

// Parameter names end with _ so they never replace the .tag / .sev designators.
#define ERR__X_ENUM(tag_, id_, sev_, ...) tag_ = (id_),
#define ERR__X_TYPE(tag_, id_, sev_, ...) typedef struct __attribute__((packed)) __VA_ARGS__ err_data_##tag_##_t;
#define ERR__X_CHECK(tag_, id_, sev_, ...) \
  _Static_assert(sizeof(err_data_##tag_##_t) <= ERR_DATA_MAX, #tag_ " data does not fit an error node");
#define ERR__X_CASE(tag_, id_, sev_, ...) \
  case (id_):                             \
    break;
#define ERR__X_INFO(tag_, id_, sev_, ...) \
  {.tag = (id_), .name = #tag_, .size = sizeof(err_data_##tag_##_t), .sev = (sev_)},

/**
 * @brief Declares a module's tags (enum) and payload types. Use in its header.
 */
#define ERR_TAGS_DECLARE(MAP) \
  enum { MAP(ERR__X_ENUM) };  \
  MAP(ERR__X_TYPE)

/**
 * @brief Defines and registers a module's tag table. Use once, in a .c file.
 * Fails to compile on a payload that does not fit a node, or two tags with the
 * same ID (duplicate case). At registration asserts that every tag belongs to
 * `mod_id` and that the module ID is not taken.
 */
#define ERR_MODULE_DEFINE(mod_id, mod_name, MAP)                                                          \
  MAP(ERR__X_CHECK)                                                                                     \
  __attribute__((unused)) static void _err_dup_check_##mod_id(uint32_t t) {                             \
    switch (t) {                                                                                        \
      MAP(ERR__X_CASE)                                                                                  \
      default:                                                                                          \
        break;                                                                                          \
    }                                                                                                   \
  }                                                                                                     \
  static const err_tag_info_t _err_tags_##mod_id[] = {MAP(ERR__X_INFO)};                                \
  static const err_module_t _err_module_##mod_id = {                                                    \
      .id = (mod_id), .name = (mod_name), .tags = _err_tags_##mod_id, .count = ARRAY_LEN(_err_tags_##mod_id)}; \
  __attribute__((constructor)) static void _err_register_##mod_id(void) {                               \
    err_register_module(&_err_module_##mod_id);                                                         \
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

// err_new() itself is declared in errors.h; its signature depends on CONFIG_ERRORS_INFO.
// Storage class in a compound literal is C2y: gcc accepts it in gnu23, clang (clangd) does not.
#ifdef __clang__
#define ERR__INFO_STORAGE
#else
#define ERR__INFO_STORAGE static
#endif
#if CONFIG_ERRORS_INFO
#define ERR__NEW(tag, data, len, cause) \
  err_new(&(ERR__INFO_STORAGE const err_info_t){ERR__INFO_FILE ERR__INFO_FUNC ERR__INFO_LINE}, (tag), (data), (len), (cause))
#else
#define ERR__NEW(tag, data, len, cause) err_new((tag), (data), (len), (cause))
#endif

/**
 * @brief New error wrapping `cause` (takes ownership of it).
 * @usage return ERR_WRAP(err, ERR_TPS_RAIL, .rail = 1, .want_mv = mv);
 */
#define ERR_WRAP(cause, tag, ...)                              \
  ({                                                           \
    const err_data_##tag##_t _err_data_ = {__VA_ARGS__};       \
    ERR__NEW((tag), &_err_data_, sizeof(_err_data_), (cause)); \
  })

/**
 * @brief New root error (no cause).
 * @usage return ERR_NEW(ERR_I2C_NACK, .addr = addr, .reg = reg);
 */
#define ERR_NEW(tag, ...) ERR_WRAP(NULL, tag, __VA_ARGS__)

/**
 * @brief Typed view of a node's payload. The caller checks e->tag first.
 * @usage if (e->tag == ERR_I2C_NACK) { uint8_t a = ERR_DATA(e, ERR_I2C_NACK)->addr; }
 */
#define ERR_DATA(e, tag) ((const err_data_##tag##_t*)(const void*)(e)->data)

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
    err_h _err_try_ = (call);      \
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

/**
 * @brief Keeps the first error of a sequence whose steps all run (teardown); a later error is freed.
 * @usage err_h err = NULL; ERR_KEEP_FIRST(err, chip_sleep(c, true)); ERR_KEEP_FIRST(err, ...);
 */
#define ERR_KEEP_FIRST(acc, call) \
  do {                            \
    err_h _err_keep_ = (call);    \
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
    esp_err_t _err_esp_rc_ = (esp_call);                                        \
    _err_esp_rc_ == ESP_OK ? NULL : ERR_NEW(ERR_ESP_ERR, .code = _err_esp_rc_); \
  })

/**
 * @brief ERR_TRY for an ESP-IDF call.
 * @usage ERR_TRY_ESP(i2c_master_transmit(h, buf, len, MSEC(10)));
 */
#define ERR_TRY_ESP(esp_call) ERR_TRY(ERR_ESP(esp_call))
