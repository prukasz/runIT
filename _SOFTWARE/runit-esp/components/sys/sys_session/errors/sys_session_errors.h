#pragma once

#include "errors.h"

// ERR_MOD_SYS_SESSION = 18 (err_modules.h). A tag about a board carries its id; one about a request
// its seq (the source's). The next hop busy, down or too short is its provider's own ERR_PROV_BUSY / _DOWN /
// _TOO_LONG.
#define ERR_SYS_SESSION_MAP(X)                                                                                         \
  X(ERR_SESSION_VERSION,      ERR_TAG(ERR_MOD_SYS_SESSION, 1),  ERR_SEV_HIGH,   { uint8_t got; uint8_t mine; }) /* hello: another format, the session stays closed */ \
  X(ERR_SESSION_NONE,         ERR_TAG(ERR_MOD_SYS_SESSION, 2),  ERR_SEV_MEDIUM, { uint8_t src; }) /* a frame before the hello: answered NO_SESSION */ \
  X(ERR_SESSION_ID_TAKEN,     ERR_TAG(ERR_MOD_SYS_SESSION, 3),  ERR_SEV_HIGH,   { uint8_t board; }) /* hello: two boards with one id */ \
  X(ERR_SESSION_NO_ROUTE,     ERR_TAG(ERR_MOD_SYS_SESSION, 4),  ERR_SEV_MEDIUM, { uint8_t dst; }) /* no session leads to dst */ \
  X(ERR_SESSION_DENIED,       ERR_TAG(ERR_MOD_SYS_SESSION, 5),  ERR_SEV_MEDIUM, { uint8_t src; }) /* a write from a read-only peer */ \
  X(ERR_SESSION_TIMEOUT,      ERR_TAG(ERR_MOD_SYS_SESSION, 6),  ERR_SEV_MEDIUM, { uint8_t dst; uint16_t seq; }) /* a call without an answer */ \
  X(ERR_SESSION_PEER_DOWN,    ERR_TAG(ERR_MOD_SYS_SESSION, 7),  ERR_SEV_MEDIUM, { uint8_t board; }) /* the session the call went through ended */ \
  X(ERR_SESSION_NOT_FRAMED,   ERR_TAG(ERR_MOD_SYS_SESSION, 8),  ERR_SEV_HIGH,   { uint8_t link; uint32_t channel; }) /* attach: the driver does not split frames */ \
  X(ERR_SESSION_PARENT_TAKEN, ERR_TAG(ERR_MOD_SYS_SESSION, 9),  ERR_SEV_HIGH,   {}) /* attach: a second parent */ \
  X(ERR_SESSION_CLIENT,       ERR_TAG(ERR_MOD_SYS_SESSION, 10), ERR_SEV_HIGH,   {}) /* attach: a client on a board with a parent, a parent on one with clients */ \
  X(ERR_SESSION_FULL,         ERR_TAG(ERR_MOD_SYS_SESSION, 11), ERR_SEV_MEDIUM, {}) /* every session or listener slot taken */ \
  X(ERR_SESSION_CALLS_FULL,   ERR_TAG(ERR_MOD_SYS_SESSION, 12), ERR_SEV_MEDIUM, {}) /* every CONFIG_SYS_SESSION_CALLS slot in use */ \
  X(ERR_REMOTE,               ERR_TAG(ERR_MOD_SYS_SESSION, 13), ERR_SEV_NONE,   { uint8_t board; uint16_t seq; }) /* context: the chain below is that board's */ \
  X(ERR_SESSION_WOULD_BLOCK,  ERR_TAG(ERR_MOD_SYS_SESSION, 14), ERR_SEV_HIGH,   {}) /* call_wait on a task that must not wait */ \
  X(ERR_SESSION_BUSY,         ERR_TAG(ERR_MOD_SYS_SESSION, 15), ERR_SEV_LOW,    { uint8_t dst; }) /* calls in flight to dst at the limit: later */

ERR_TAGS_DECLARE(ERR_SYS_SESSION_MAP)
