#pragma once

#include "errors.h"
#include "sys_session_id.h"

// A tag about a board carries its id; one about a request its seq (the source's). The provider busy, down or too
// short is its provider's own ERR_PROV_BUSY / _DOWN / _TOO_LONG.
#define ERR_SYS_SESSION_MAP(X, M)                                                                                                      \
  X(M, ERR_SESSION_ID_TAKEN,    1, ERR_SEV_HIGH,   { uint8_t board; }) /* hello: two peers with one id, or an id of the wrong kind */  \
  X(M, ERR_SESSION_NO_ROUTE,    2, ERR_SEV_MEDIUM, { uint8_t dst; }) /* no session leads to dst */                                     \
  X(M, ERR_SESSION_FULL,        3, ERR_SEV_MEDIUM, {}) /* every session slot taken */                                                  \
  X(M, ERR_SESSION_NOT_FRAMED,  4, ERR_SEV_MEDIUM, {}) /* attach: no provider, or a raw one: no whole frame */

ERR_TAGS_DECLARE(MODULE_ID_SESSION, ERR_SYS_SESSION_MAP)
