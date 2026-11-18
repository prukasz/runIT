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

/**
 * @brief One error in a chain, opaque: its layout is the errors component's own (err_internal.h). A node has a tag
 * (ERR_TAG(module, code), err_tag), the tag's payload (err_data, ERR_DATA) and its cause, the deeper error
 * (err_cause, NULL at the root cause). Only ERR_NEW / ERR_WRAP make one; whoever holds it returns it, frees it
 * (err_free) or reports it (err_report).
 */
typedef struct err* err_h;

/**
 * @brief The node's tag, 0 for NULL.
 */
uint32_t err_tag(err_h e);

/**
 * @brief The node's cause (the next, deeper node), NULL at the root cause and for NULL.
 */
err_h err_cause(err_h e);

/**
 * @brief The node's payload, as its tag's map declares it (err_data_<TAG>_t; ERR_DATA gives it typed). NULL for NULL.
 */
const void* err_data(err_h e);

/**
 * @brief The first node of the chain with `tag`, from the top, or NULL when no node has it.
 * @usage err_h n = err_find(chain, ERR_DEV_ID); if (n != NULL) { id = ERR_DATA(n, ERR_DEV_ID)->id; }
 */
err_h err_find(err_h chain, uint32_t tag);

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

#include "core_errors.h"  // the core tags (module 0): errors/, as every module's map

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
