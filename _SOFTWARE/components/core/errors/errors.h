#pragma once

#include "esp_err.h"
#include "sdkconfig.h"
#include "utils.h"
#include "err_macros.h"   // all macros: ERR_TAG, ERR_NEW/WRAP, ERR_TRY*, ERR_MODULE_DEFINE, ...
#include "err_modules.h"  // module IDs (ERR_MOD_*)

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_ERRORS_INFO
/**
 * @brief Where the error was created. One static const instance per call site
 * (lives in flash), the node only points to it. Only the fields enabled in
 * Kconfig (CONFIG_ERRORS_INFO_FILE/FUNC/LINE) exist. With all three off there
 * is no err_info_t and no `info` field in err_t.
 */
typedef struct {
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
 * NULL at the root cause. `tag` is ERR_TAG(module, code). `sev` is the
 * severity set where the node was created, or ERR_SEV_DEFAULT to use the
 * tag's own (see err_sev / err_chain_sev). `data` is the tag's payload, stored
 * right after the node.
 *
 * Packed - no padding anywhere, `data` starts right after `sev`. aligned(4)
 * keeps the node itself 4-aligned, so the compiler still uses plain 32-bit
 * loads for the pointer and tag fields (packed alone would make every field
 * access byte-wise). Payload types are packed too, so `data` needs no
 * alignment.
 */
struct __attribute__((packed, aligned(4))) err {
#if CONFIG_ERRORS_INFO
  const err_info_t* info;
#endif
  err_h next;
  uint32_t tag;
  uint8_t sev;  // err_sev_e
  uint8_t data[];
};

_Static_assert(offsetof(err_t, data) == ERR_HDR_LEN, "err_t header has padding - data must follow sev directly");

/**
 * @brief Severity. Each tag has a default - its weight without context. The
 * place that creates a node may set another one (ERR_NEW_SEV / ERR_WRAP_SEV):
 * only it knows what the failure costs there (a failed malloc drops one log
 * line in the logger, but kills a device in its create). How a device reacts
 * is decided by policy later.
 */
//#ref-enum @alias Error Severity
typedef enum {
  ERR_SEV_NONE = 0,  //@alias None @description No severity: context only (a route, a request)
  ERR_SEV_LOW = 1,  //@alias Low @description Noteworthy; nothing needs to stop
  ERR_SEV_MEDIUM = 2,  //@alias Medium @description Something failed and a feature may not work
  ERR_SEV_HIGH = 3,  //@alias High @description A significant failure; the board may need attention
  ERR_SEV_CRITICAL = 4,  //@alias Critical @description The board must be made safe
  ERR_SEV_DEFAULT = 0xFF,  //@alias Tag default @description The severity the error's tag declares
} err_sev_e;

typedef struct {
  uint32_t tag;
  const char* name;  // tag name as written in the map, e.g. "ERR_I2C_NACK"
  uint8_t size;      // sizeof(err_data_<TAG>_t)
  uint8_t sev;       // err_sev_e
} err_tag_info_t;

typedef struct {
  uint16_t id;
  const char* name;
  const err_tag_info_t* tags;
  uint16_t count;
} err_module_t;

/**
 * @brief Adds a module's table to the registry. Called by ERR_MODULE_DEFINE.
 */
void err_register_module(const err_module_t* module);

/**
 * @brief Looks a tag up in the registered tables.
 * @return Its info, or NULL for an unknown tag.
 */
const err_tag_info_t* err_tag_info(uint32_t tag);

/**
 * @brief Registered module by ID, or NULL.
 */
const err_module_t* err_module(uint16_t id);

/**
 * @brief Severity of one node: the one set at its creation, else its tag's
 * default (ERR_SEV_NONE for an unknown tag). NULL gives ERR_SEV_NONE.
 */
uint8_t err_sev(err_h e);

/**
 * @brief Severity of a whole chain. Walks from the outermost node: nodes
 * without a set severity add their tag default (highest wins); the first node
 * with a set severity adds that and ends the walk - it is that level's verdict
 * on everything beneath it, so it can raise or lower its causes. Nodes above
 * it still count, they did not reconsider it. NULL gives ERR_SEV_NONE.
 * @usage
 *   ERR_WRAP_SEV(e, ERR_DEV_CREATE, ERR_SEV_LOW, ...) over a HIGH i2c error -> LOW
 *   ERR_WRAP(e, ERR_DEV_CREATE, ...) (default HIGH) over a LOW node          -> HIGH
 */
uint8_t err_chain_sev(err_h chain);

/* ----------------------------------------------------------------------------
 * Core tags (module 0)
 * ------------------------------------------------------------------------- */

#define ERR_CORE_MAP(X)                                                                         \
  X(ERR_POOL_EMPTY, ERR_TAG(ERR_MOD_CORE, 1), ERR_SEV_LOW, {}) /* no block for a new error */ \
  X(ERR_ESP_ERR, ERR_TAG(ERR_MOD_CORE, 2), ERR_SEV_HIGH, { esp_err_t code; }) /* ESP-IDF call failed */

ERR_TAGS_DECLARE(ERR_CORE_MAP)

/* ----------------------------------------------------------------------------
 * Nodes
 * ------------------------------------------------------------------------- */

#if CONFIG_ERRORS_INFO
/**
 * @brief Creates a node. Use the ERR_NEW / ERR_WRAP macros instead.
 * @param info  Call-site info (static, see ERR__NEW).
 * @param tag   ERR_TAG(module, code).
 * @param sev   err_sev_e, or ERR_SEV_DEFAULT for the tag's own.
 * @param data  Payload, copied into the node. May be NULL when len == 0.
 * @param len   Payload length, at most ERR_DATA_MAX (longer is cut).
 * @param cause Deeper error; ownership passes to the new node. NULL for none.
 */
MUST_USE err_h err_new(const err_info_t* info, uint32_t tag, uint8_t sev, const void* data, uint32_t len,
                       err_h cause);
#else
MUST_USE err_h err_new(uint32_t tag, uint8_t sev, const void* data, uint32_t len, err_h cause);
#endif

/**
 * @brief Frees a whole chain (the node and all its causes). NULL is ignored.
 */
void err_free(err_h chain);

/**
 * @brief Prints a chain to the log, outermost node first, one line per node:
 * tag name, module.code, severity (with the tag default when it was set at
 * creation), payload in hex, and file:line func() where the info is enabled.
 * The whole chain uses one log level, from err_chain_sev (HIGH/CRITICAL -
 * error, MEDIUM - warning, else info). Does not free the chain. Task context
 * only (uses ESP_LOG). NULL is ignored.
 */
void err_log(err_h chain);

/**
 * @brief Switches the log lines of chains (err_log, err_log_ctx, so also err_report) on or off; on at boot. A chain
 * still reaches the sink and is freed: only the text stops. The level of the lines is ESP_LOG's own.
 */
void err_log_enable(bool on);

/**
 * @brief err_log with a context line first (e.g. the command that failed),
 * at the same log level as the chain. `context` NULL = plain err_log.
 */
void err_log_ctx(err_h chain, const char* context);

/* ----------------------------------------------------------------------------
 * Reporting: the end of a chain nobody returns any more
 * ------------------------------------------------------------------------- */

/**
 * @brief Where reported chains go besides the log (the app sends them to the
 * errors stream). `origin` is what the reporter passed (the router passes its
 * router_cmd_t), NULL for an internal error. Must not report errors itself.
 */
typedef void (*err_sink_f)(err_h chain, const void* origin);

/**
 * @brief Sets the sink. Boot only. NULL = log only.
 */
void err_set_sink(err_sink_f fn);

/**
 * @brief Ends a chain: logs it (with `log_ctx` first, may be NULL), passes it
 * to the sink with `origin`, frees it. NULL is ignored. Task context only.
 * @usage err_report(sys_i2c_probe(bus, addr));   // nobody to return it to
 */
void err_report_ex(err_h chain, const char* log_ctx, const void* origin);
void err_report(err_h chain);

/**
 * @brief Encodes a chain for the wire, outermost node first:
 *   [count u8 (bit 7 = truncated)] then per node
 *   [tag u32 LE][sev u8 (err_sev)][len u8][payload, len bytes]
 * Payload is the packed err_data_<TAG>_t as is (len = its size, 0 for an
 * unknown tag). Nodes that do not fit `cap` are left out and the truncated bit
 * is set. Does not free the chain.
 * @return Bytes written (0 only when cap == 0).
 */
uint32_t err_encode(err_h chain, uint8_t* buf, uint32_t cap);

#ifdef __cplusplus
}
#endif
