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
 * NULL at the root cause. `tag` is ERR_TAG(module, code); its severity is the
 * tag's (err_sev). `data` is the tag's payload, stored right after the node.
 *
 * Packed - no padding anywhere, `data` starts right after `tag`. aligned(4)
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
  uint8_t data[];
};

_Static_assert(offsetof(err_t, data) == ERR_HDR_LEN, "err_t header has padding - data must follow sev directly");

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
 * @brief Severity of one node: its tag's (ERR_SEV_NONE for an unknown tag or NULL).
 */
uint8_t err_sev(err_h e);

/**
 * @brief Severity of a whole chain: the highest of its nodes. NULL gives ERR_SEV_NONE.
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
 * @param data  Payload, copied into the node. May be NULL when len == 0.
 * @param len   Payload length, at most ERR_DATA_MAX (longer is cut).
 * @param cause Deeper error; ownership passes to the new node. NULL for none.
 */
MUST_USE err_h err_new(const err_info_t* info, uint32_t tag, const void* data, uint32_t len, err_h cause);
#else
MUST_USE err_h err_new(uint32_t tag, const void* data, uint32_t len, err_h cause);
#endif

/**
 * @brief Frees a whole chain (the node and all its causes). NULL is ignored.
 */
void err_free(err_h chain);

/**
 * @brief Prints a chain to the log, outermost node first, one line per node:
 * tag name, module.code, severity, payload in hex, and file:line func() where
 * the info is enabled.
 * The whole chain uses one log level, from err_chain_sev (HIGH/CRITICAL -
 * error, MEDIUM - warning, else info). Does not free the chain. Task context
 * only (uses ESP_LOG). NULL is ignored.
 */
void err_log(err_h chain);

/**
 * @brief Switches the log lines of chains (err_log, so also err_report) on or off; on at boot. A chain
 * still reaches the sink and is freed: only the text stops. The level of the lines is ESP_LOG's own.
 */
void err_log_enable(bool on);

/* ----------------------------------------------------------------------------
 * The command an error belongs to
 * ------------------------------------------------------------------------- */

/** A command as it came in: its link, seq, flags and request (keys + payload, no seq / flags). The router fills it. */
typedef struct {
  const uint8_t* req;  // may be NULL when req_len is 0
  uint32_t req_len;
  uint8_t link;
  uint8_t seq;
  uint8_t flags;
} err_cmd_t;

/* ----------------------------------------------------------------------------
 * Reporting: the end of a chain nobody returns any more
 * ------------------------------------------------------------------------- */

/**
 * @brief Where reported chains go besides the log (the app sends them to the
 * errors stream). `cmd` is the command the chain belongs to, NULL for an
 * internal error. Must not report errors itself.
 */
typedef void (*err_sink_f)(err_h chain, const err_cmd_t* cmd);

/**
 * @brief Sets the sink. Boot only. NULL = log only.
 */
void err_set_sink(err_sink_f fn);

/**
 * @brief Ends a chain: logs it (with `log_ctx` first, may be NULL), passes it
 * to the sink with `cmd` (may be NULL), frees it. NULL is ignored. Task context only.
 * @usage err_report(sys_i2c_probe(bus, addr));   // nobody to return it to
 */
void err_report_ex(err_h chain, const char* log_ctx, const err_cmd_t* cmd);
void err_report(err_h chain);

/* ----------------------------------------------------------------------------
 * Error record (ERRORS.MD "Error record"): a chain with what caused it, the
 * same bytes in an ERROR answer, on the errors stream and in the history.
 * ------------------------------------------------------------------------- */

//#ref-enum @alias Error Record Source
typedef enum {
  ERR_RECORD_SRC_INTERNAL = 0,  //@alias Internal @description No request (task, timer, boot); later device ID/task may follow
  ERR_RECORD_SRC_COMMAND = 1,  //@alias Command @description Raised while running a command
} err_record_src_e;

//#ref-defines err_record_flag @alias Error Record Flags @prefix ERR_RECORD_
#define ERR_RECORD_REQ_CUT 0x80u  //@alias Request cut @description Bit 7 of request length: request was cut to fit, more was sent
#define ERR_RECORD_STAMP_BYTES 8u  //@alias Time stamp bytes @description The u64 LE UTC time in ms (the board's clock) right after the source byte; counts from the start while no client has set the clock (before 2020: not a date)

/**
 * @brief Encodes an error record into `buf`: [src][stamp] (+ [link][seq][flags][req_len][request] for a command), then
 * the chain, outermost node first: [count u8, bit 7 = cut] and per node [tag u32 LE][len u8][payload]
 * (the packed err_data_<TAG>_t; len 0 for an unknown tag). The request is cut to CONFIG_ERRORS_RECORD_REQ_MAX and
 * further when `cap` is small; nodes that don't fit are left out and bit 7 is set. Does not free the chain.
 * @param cmd The command, or NULL for an internal error.
 * @return Bytes written (0 only when cap is too small for the header).
 */
uint32_t err_record(const err_cmd_t* cmd, err_h chain, uint8_t* buf, uint32_t cap);

#ifdef __cplusplus
}
#endif
