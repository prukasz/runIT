/*
 * The command binding (SYS_DATA_PROVIDER.MD "Command binding"): a provider's third user, beside a session and a
 * stream's binding. What comes in on it is one command, as the router takes it: [key][dir][code | id + op][args], no
 * header. It goes to the router with src ROUTER_SRC_NONE; its answer goes back on the same provider as
 * [status][data], status router_status_e and data the answer or, on ERROR, the error record, as every answer has.
 * The router knows nothing of providers: the answer finds its way by the callback's context, the binding's slot.
 */
#include <string.h>

#include "sdkconfig.h"
#include "router.h"
#include "sys_data_provider.h"
#include "utils.h"

#define STATUS_BYTE 1u     // an answer is [status][data]
#define ERROR_MAX   160u   // a refused command's error record, on the RX task's stack

// A bound provider. One slot per provider at most: the table never fills before the registry.
typedef struct {
  bool used;
  uint16_t gen;  // changes when the slot is taken again: an answer for the binding before it is dropped
  uint16_t seq;  // commands come in: each one's seq, so its error record names it
  uint8_t id;
} binding_t;

static binding_t s_bind[CONFIG_SYS_DATA_PROVIDER_MAX];
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;  // the table: short copies only

#define CTX(slot, gen) ((void*)(uintptr_t)(((uint32_t)(gen) << 8) | (slot)))
#define CTX_SLOT(ctx)  ((uint8_t)((uintptr_t)(ctx) & 0xFFu))
#define CTX_GEN(ctx)   ((uint16_t)((uintptr_t)(ctx) >> 8))

// The provider of the binding the context names, while it is that binding. false: unbound since.
static bool binding_of(void* ctx, uint8_t* id) {
  const uint8_t slot = CTX_SLOT(ctx);
  if (slot >= CONFIG_SYS_DATA_PROVIDER_MAX) {
    return false;
  }
  portENTER_CRITICAL(&s_lock);
  const bool same = s_bind[slot].used && s_bind[slot].gen == CTX_GEN(ctx);
  *id = s_bind[slot].id;
  portEXIT_CRITICAL(&s_lock);
  return same;
}

/* The answer, on the router's task: `data` has CMD_HEAD writable bytes in front of it, where the status goes, so it
   is sent without a copy. NOWAIT: a full driver queue loses it (the router reports that with the command). */
static err_h on_answer(void* ctx, const cmd_req_t* cmd, uint8_t status, uint8_t* data, uint32_t len) {
  (void)cmd;
  uint8_t id;
  if (!binding_of(ctx, &id)) {
    return NULL;  // unbound meanwhile: nobody to answer
  }
  data[-1] = status;
  return sys_data_provider_send(id, data - STATUS_BYTE, len + STATUS_BYTE, SYS_DATA_PROVIDER_SEND_NOWAIT);
}

// One message in, on the driver's RX task: a command for the router. One the router refuses is answered here.
static void on_rx(void* ctx, uint8_t id, const uint8_t* data, uint32_t len) {
  if (data == NULL) {
    return;  // went up or down
  }
  const uint8_t slot = CTX_SLOT(ctx);
  portENTER_CRITICAL(&s_lock);
  const uint16_t seq = s_bind[slot].seq++;
  portEXIT_CRITICAL(&s_lock);
  const cmd_req_t cmd = {.req = data, .req_len = len, .seq = seq, .src = ROUTER_SRC_NONE, .flags = 0};
  err_h err = router_submit(&cmd, on_answer, ctx);
  if (err == NULL) {
    return;
  }
  uint8_t buf[STATUS_BYTE + ERROR_MAX];
  buf[0] = ROUTER_STATUS_ERROR;
  const uint32_t n = cmd_err_record(&cmd, err, buf + STATUS_BYTE, ERROR_MAX);
  err_free(sys_data_provider_send(id, buf, STATUS_BYTE + n, SYS_DATA_PROVIDER_SEND_NOWAIT));
  router_report_cmd(&cmd, err);  // as the session does with a command the router refused
}

err_h sys_data_provider_bind_commands(uint8_t id) {
  int at = -1;
  portENTER_CRITICAL(&s_lock);
  for (int i = 0; i < CONFIG_SYS_DATA_PROVIDER_MAX && at < 0; i++) {
    at = s_bind[i].used ? -1 : i;
  }
  if (at >= 0) {
    s_bind[at].used = true;  // taken now, so a second bind of another provider gets another slot
    s_bind[at].gen++;
    s_bind[at].seq = 0;
    s_bind[at].id = id;
  }
  const uint16_t gen = at >= 0 ? s_bind[at].gen : 0;
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(at >= 0, ERR_PROV_FULL);
  // The rx hook has one owner: another one's (a session, an IN stream, this binding already) is ERR_PROV_IN_USE.
  err_h err = sys_data_provider_open(id, on_rx, CTX(at, gen));
  if (err != NULL) {
    portENTER_CRITICAL(&s_lock);
    s_bind[at].used = false;
    portEXIT_CRITICAL(&s_lock);
  }
  return err;
}

err_h sys_data_provider_unbind_commands(uint8_t id) {
  bool found = false;
  portENTER_CRITICAL(&s_lock);
  for (int i = 0; i < CONFIG_SYS_DATA_PROVIDER_MAX && !found; i++) {
    if (s_bind[i].used && s_bind[i].id == id) {
      s_bind[i].used = false;
      s_bind[i].gen++;
      found = true;
    }
  }
  portEXIT_CRITICAL(&s_lock);
  return found ? sys_data_provider_close(id) : NULL;
}
