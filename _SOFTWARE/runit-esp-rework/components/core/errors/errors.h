#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "sdkconfig.h"

#ifndef MUST_USE
#define MUST_USE __attribute__((warn_unused_result))
#endif

#include "err_macros.h"   // tags, creating and propagating: ERR_TAG, ERR_NEW/WRAP, ERR_TRY, ERR_CHECK*, ...

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_ERRORS_INFO
/**
 * @brief Error diagnostics - the node only points to it. Only the fields enabled in
 * Kconfig (CONFIG_ERRORS_INFO_FILE/FUNC/LINE) exist. With all three off there
 * is no err_info_t and no `info` field in err_t.
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

typedef struct err err_t;
typedef err_t* err_h;

/**
 * @brief One error in a chain. `next` points to the cause (the deeper error),
 * NULL at the root cause. `tag` is ERR_TAG(module, code); its severity is the
 * tag's (err_sev). `data` is the tag's payload, stored right after the node.
 * `info` (where enabled) is NULL only for the static ERR_POOL_EMPTY node.
 *
 * Packed - no padding anywhere, `data` starts right after `tag`. aligned(4)
 */
struct __attribute__((packed, aligned(4))) err {
#if CONFIG_ERRORS_INFO
  const err_info_t* info;
#endif
  err_h next;
  uint32_t tag;
  uint8_t data[];
};

/**
 * @brief Severity: each tag has one, in its module's map. How a device reacts
 * to it is decided by its error policy (SYS_DEVICE.MD).
 */
//#ref-enum @alias Error Severity
typedef enum {
  ERR_SEV_NONE = 0,  //@alias None @description No severity: context only (a route, a request)
  ERR_SEV_LOW = 1,  //@alias Low @description Noteworthy; nothing needs to stop
  ERR_SEV_MEDIUM = 2,  //@alias Medium @description Something failed and a feature may not work
  ERR_SEV_HIGH = 3,  //@alias High @description A significant failure; the board may need attention
  ERR_SEV_CRITICAL = 4,  //@alias Critical @description The board must be made safe
} err_sev_e;

/**
 * @brief Severity of one node: its tag's (ERR_SEV_NONE for an unknown tag).
 */
uint8_t err_sev(err_h e);

/**
 * @brief Severity of a whole chain: the highest of its nodes. NULL gives ERR_SEV_NONE.
 */
uint8_t err_chain_sev(err_h chain);

/**
 * @brief Tag of the chain's root cause (its deepest node), 0 for NULL.
 */
uint32_t err_root_tag(err_h chain);

/**
 * @brief Takes away an error that is an answer, not a failure: when the root cause of *err is `tag` (a probe that
 * found nothing, an op the device doesn't have), the chain is freed and *err becomes NULL. Any other error, and NULL,
 * is left as it is.
 * @param[in,out] err The chain, may be NULL
 * @param[in]     tag The tag of the cause the caller tolerates
 * @return true when *err had that cause and is gone
 * @usage (void)err_drop(&err, ERR_DEV_UNKNOWN_OP); return err;
 *        if (err_drop(&err, ERR_I2C_DEV_NOT_FOUND)) { continue; } ERR_TRY(err);
 */
bool err_drop(err_h* err, uint32_t tag);

/* ----------------------------------------------------------------------------
 * Core tags (module 0)
 * ------------------------------------------------------------------------- */

/** @brief The module of the core errors: 0, no module (module_id.h). */
enum { MODULE_ID_CORE = 0 };

#define ERR_CORE_MAP(X, M)                                                                                              \
  X(M, ERR_POOL_EMPTY, 1, ERR_SEV_LOW,    {}) /* no block for a new error */                                            \
  X(M, ERR_ESP_ERR,    2, ERR_SEV_HIGH,   { esp_err_t code; }) /* ESP-IDF call failed */                                \
  X(M, ERR_RECORD_CUT, 3, ERR_SEV_MEDIUM, { uint16_t len; }) /* err_record_parse: cut short */                          \
  X(M, ERR_NULL_PTR,   4, ERR_SEV_HIGH,   {}) /* a C function got a NULL pointer: its caller's bug */                   \
  X(M, ERR_RANGE,      5, ERR_SEV_HIGH,   { int32_t value; int32_t min; int32_t max; }) /* ERR_CHECK_RANGE */           \
  X(M, ERR_RANGE_F,    6, ERR_SEV_HIGH,   { float value; float min; float max; }) /* ERR_CHECK_RANGE, float v */

ERR_TAGS_DECLARE(MODULE_ID_CORE, ERR_CORE_MAP)

/* ----------------------------------------------------------------------------
 * Nodes
 * ------------------------------------------------------------------------- */

/**
 * @brief Frees a whole chain (the node and all its causes). NULL is ignored.
 */
void err_free(err_h chain);

/* ----------------------------------------------------------------------------
 * Reporting: the end of a chain nobody returns any more
 * ------------------------------------------------------------------------- */

/**
 * @brief Ends a chain: logs it, passes it to the sink (err_sink.h), frees it. NULL is ignored. Task context only.
 * @usage err_report(sys_i2c_probe(bus, addr));   // nobody to return it to
 */
void err_report(err_h chain);

#ifdef __cplusplus
}
#endif
