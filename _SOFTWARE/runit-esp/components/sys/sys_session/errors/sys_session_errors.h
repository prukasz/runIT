#pragma once

#include "errors.h"
#include "sys_session_id.h"

// A tag about a board carries its id; one about a request its seq (the source's). The provider busy, down or too
// short is its provider's own ERR_PROV_BUSY / _DOWN / _TOO_LONG.
#define ERR_SYS_SESSION_MAP(X, M)                                                                                                      \
  X(M, ERR_SESSION_ID_TAKEN,    3,  ERR_SEV_HIGH,   { uint8_t board; }) /* hello: two peers with one id, or an id of the wrong kind */ \
  X(M, ERR_SESSION_NO_ROUTE,    4,  ERR_SEV_MEDIUM, { uint8_t dst; }) /* no session leads to dst */                                    \
  X(M, ERR_SESSION_TIMEOUT,     6,  ERR_SEV_MEDIUM, { uint8_t dst; uint16_t seq; }) /* a call without an answer */                     \
  X(M, ERR_SESSION_PEER_DOWN,   7,  ERR_SEV_MEDIUM, { uint8_t board; }) /* the session the call went through ended */                  \
  X(M, ERR_SESSION_FULL,        11, ERR_SEV_MEDIUM, {}) /* every session slot taken */                                                 \
  X(M, ERR_SESSION_CALLS_FULL,  12, ERR_SEV_MEDIUM, {}) /* every CONFIG_SYS_SESSION_CALLS slot in use */                               \
  X(M, ERR_REMOTE,              13, ERR_SEV_NONE,   { uint8_t board; uint16_t seq; }) /* context: the chain below is that board's */   \
  X(M, ERR_SESSION_WOULD_BLOCK, 14, ERR_SEV_HIGH,   {}) /* call_wait to this board on a router task: it would wait for itself */       \
  X(M, ERR_SESSION_NOT_FRAMED,  16, ERR_SEV_MEDIUM, {}) /* attach: a raw provider carries no whole frame */                            \
  X(M, ERR_SESSION_NOT_OPEN,    17, ERR_SEV_LOW,    { uint8_t provider; }) /* push: no open session on the provider */

ERR_TAGS_DECLARE(MODULE_ID_SESSION, ERR_SYS_SESSION_MAP)
