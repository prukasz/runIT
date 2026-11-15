/*
 * The command binding (SYS_DATA_PROVIDER.MD "Command binding"): a provider's third user, beside a session and a
 * stream's source. What comes in on it is one command, as the router takes it: [key][dir][code | id + op][args], no
 * header. It goes to the router with src ROUTER_SRC_NONE; its answer goes back on the same provider as
 * [status][data], status router_status_e and data the answer or, on ERROR, the error record, as every answer has.
 */
#include <string.h>

#include "router_queue.h"
#include "sdkconfig.h"
#include "sys_data_provider.h"
#include "sys_data_provider_internal.h"
#include "utils.h"

#define ERROR_MAX 160u  // a refused command's error record, on the RX task's stack

/* The bound providers: one entry per provider at most, so the table never fills before the registry. */
static struct {
  bool used;
  uint8_t id;
} s_bound[CONFIG_SYS_DATA_PROVIDER_MAX];
static uint16_t s_seq;  // the commands that came in: each one's seq, so its error record names it
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

// Marks `id` bound or not; false when there was nothing to change.
static bool set_bound(uint8_t id, bool bound) {
  bool done = false;
  portENTER_CRITICAL(&s_lock);
  for (uint32_t i = 0; i < ARRAY_LEN(s_bound) && !done; i++) {
    if (bound ? !s_bound[i].used : (s_bound[i].used && s_bound[i].id == id)) {
      s_bound[i].used = bound;
      s_bound[i].id = id;
      done = true;
    }
  }
  portEXIT_CRITICAL(&s_lock);
  return done;
}

static bool is_bound(uint8_t id) {
  bool bound = false;
  portENTER_CRITICAL(&s_lock);
  for (uint32_t i = 0; i < ARRAY_LEN(s_bound); i++) {
    bound = bound || (s_bound[i].used && s_bound[i].id == id);
  }
  portEXIT_CRITICAL(&s_lock);
  return bound;
}

static err_h send_answer(uint8_t id, uint8_t status, const uint8_t* data, uint32_t len) {
  ERR_CHECK(len < CMD_FRAME_MAX, ERR_PROV_TOO_LONG, .provider = id, .len = (uint16_t)(len + 1u), .max = CMD_FRAME_MAX);
  uint8_t frame[1u + len];
  frame[0] = status;
  memcpy(frame + 1u, data, len);
  return sys_data_provider_send(id, (cmd_in_t){frame, sizeof(frame)}, CMD_SEND_NOWAIT);
}

/* The answer, on the router's task; ctx is the provider. NOWAIT: a full driver queue loses it (the router reports
   that with the command). Unbound meanwhile: nobody to answer. */
static err_h on_answer(void* ctx, const cmd_req_t* cmd, uint8_t status, const uint8_t* data, uint32_t len) {
  (void)cmd;
  const uint8_t id = (uint8_t)(uintptr_t)ctx;
  return is_bound(id) ? send_answer(id, status, data, len) : NULL;
}

// One message in, on the driver's RX task: a command for the router. One the router refuses is answered here.
static void on_rx(void* ctx, uint8_t id, const uint8_t* data, uint32_t len) {
  (void)ctx;
  if (data == NULL) {
    return;  // went up or down
  }
  portENTER_CRITICAL(&s_lock);
  const uint16_t seq = s_seq++;
  portEXIT_CRITICAL(&s_lock);
  const cmd_req_t cmd = {.req = data, .req_len = len, .seq = seq, .src = ROUTER_SRC_NONE};
  err_h err = router_submit(&cmd, on_answer, (void*)(uintptr_t)id);
  if (err != NULL) {
    uint8_t record[ERROR_MAX];
    err_free(send_answer(id, ROUTER_STATUS_ERROR, record, cmd_err_record(&cmd, err, record, sizeof(record))));
    router_report_cmd(&cmd, err);  // as the session does with a command the router refused
  }
}

// The rx hook has one owner: another one's (a session, a stream's source, this binding already) is ERR_PROV_IN_USE.
err_h sys_data_provider_bind_commands(uint8_t id) {
  ERR_TRY(sys_data_provider_open(id, on_rx, NULL));
  (void)set_bound(id, true);
  return NULL;
}

err_h sys_data_provider_unbind_commands(uint8_t id) {
  return set_bound(id, false) ? sys_data_provider_close(id) : NULL;
}
