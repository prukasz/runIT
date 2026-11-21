/* Host tests of sys_log (ANSI cleaning, config validation, error history ring, data_matrix push & relay). */
#include <stdarg.h>
#include <string.h>

#include "data_matrix.h"
#include "errors_id.h"
#include "esp_log.h"
#include "host_assert.h"
#include "router.h"
#include "sdkconfig.h"
#include "sys_log.h"
#include "sys_log_router.h"
#include "unity.h"

static vprintf_like_t s_log_hook;

/* Capture what sys_log pushes to MODULE_ID_LOG and MODULE_ID_ERRORS */
typedef struct {
  data_matrix_port_t port;
  int calls;
  uint8_t last_flags;
  uint8_t last_data[CMD_FRAME_MAX];
  uint32_t last_len;
} sink_t;

static void sink_in(data_matrix_port_t* self, data_matrix_port_id_t from, uint8_t flags, const uint8_t* data,
                    uint32_t len) {
  (void)from;
  sink_t* s = DATA_MATRIX_OWNER(self, sink_t, port);
  s->calls++;
  s->last_flags = flags;
  s->last_len = len < sizeof(s->last_data) ? len : sizeof(s->last_data);
  if (s->last_len > 0) {
    memcpy(s->last_data, data, s->last_len);
  }
}

static sink_t s_log_sink = {.port = {.id = DATA_MATRIX_PORT(DATA_MATRIX_PORT_PROVIDER, 1), .in = sink_in}};
static sink_t s_err_sink_a = {.port = {.id = DATA_MATRIX_PORT(DATA_MATRIX_PORT_PROVIDER, 2), .in = sink_in}};
static sink_t s_err_sink_b = {.port = {.id = DATA_MATRIX_PORT(DATA_MATRIX_PORT_PROVIDER, 3), .in = sink_in}};

static void emit_vprintf(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  s_log_hook(fmt, ap);
  va_end(ap);
}

static err_h route_log(uint8_t dir, uint8_t op, const void* args, uint32_t args_len, uint8_t* reply,
                       uint32_t reply_cap, uint32_t* reply_len) {
  uint8_t req[32];
  req[0] = MODULE_ID_LOG;
  req[1] = dir;
  req[2] = op;
  if (args_len > 0) {
    memcpy(req + 3, args, args_len);
  }
  cmd_out_t out = {.data = reply, .cap = reply_cap, .len = 0};
  err_h e = router_route((cmd_in_t){req, 3u + args_len}, &out);
  if (reply_len != NULL) {
    *reply_len = out.len;
  }
  return e;
}

void setUp(void) {
  s_log_sink.calls = 0;
  s_err_sink_a.calls = 0;
  s_err_sink_b.calls = 0;
}

void tearDown(void) {}

static void test_config_validation_and_router(void) {
  sys_log_cfg_t cfg = {0};
  uint8_t reply[32];
  uint32_t rlen = 0;

  /* Default config */
  TEST_ASSERT_NULL(route_log(CMD_DIR_READ, LOG_OP_CONFIG, NULL, 0, reply, sizeof(reply), &rlen));
  TEST_ASSERT_EQUAL_UINT32(sizeof(cfg), rlen);
  memcpy(&cfg, reply, sizeof(cfg));
  TEST_ASSERT_EQUAL_UINT8(CONFIG_LOG_DEFAULT_LEVEL, cfg.level);
  TEST_ASSERT_EQUAL_UINT8(1, cfg.trace_errors);

  /* Valid write updates level & applies to esp_log_level_set */
  cfg = (sys_log_cfg_t){.level = 4, .mirror_serial = 1, .trace_errors = 0};
  TEST_ASSERT_NULL(route_log(CMD_DIR_WRITE, LOG_OP_CONFIG, &cfg, sizeof(cfg), reply, sizeof(reply), &rlen));
  TEST_ASSERT_EQUAL_INT(4, host_log_level_get());

  /* Above CONFIG_LOG_MAXIMUM_LEVEL -> ERR_RANGE */
  cfg.level = CONFIG_LOG_MAXIMUM_LEVEL + 1;
  err_h e = route_log(CMD_DIR_WRITE, LOG_OP_CONFIG, &cfg, sizeof(cfg), reply, sizeof(reply), &rlen);
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_EQUAL_HEX32(ERR_RANGE, err_root_tag(e));
  err_free(e);

  /* Non-boolean mirror_serial -> ERR_BAD_ARG */
  cfg = (sys_log_cfg_t){.level = 3, .mirror_serial = 2, .trace_errors = 1};
  e = route_log(CMD_DIR_WRITE, LOG_OP_CONFIG, &cfg, sizeof(cfg), reply, sizeof(reply), &rlen);
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_EQUAL_HEX32(ERR_BAD_ARG, err_root_tag(e));
  err_free(e);
}

static void test_log_vprintf_ansi_stripping_and_unlinked_skip(void) {
  const data_matrix_link_t lnk = {DATA_MATRIX_PORT(DATA_MATRIX_PORT_MODULE, MODULE_ID_LOG), s_log_sink.port.id};

  /* While unlinked, emitting produces nothing */
  emit_vprintf("\x1b[0;32mI (10) tag: hi\x1b[0m\r\n");
  TEST_ASSERT_EQUAL_INT(0, s_log_sink.calls);

  /* Link LOG -> sink: ANSI codes and trailing CR/LF stripped, PUSH header prepended */
  TEST_ASSERT_NULL(data_matrix_connect(&lnk));
  emit_vprintf("\x1b[0;32mI (10) tag: hello world\x1b[0m\r\n");
  TEST_ASSERT_EQUAL_INT(1, s_log_sink.calls);
  TEST_ASSERT_EQUAL_HEX8(CMD_SEND_NOWAIT, s_log_sink.last_flags);
  TEST_ASSERT_EQUAL_UINT8(CMD_PUSH, s_log_sink.last_data[0] >> 6);
  TEST_ASSERT_EQUAL_UINT8(MODULE_ID_LOG, s_log_sink.last_data[2]);
  const char* want = "I (10) tag: hello world";
  TEST_ASSERT_EQUAL_UINT32(CMD_HEAD + strlen(want), s_log_sink.last_len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want, s_log_sink.last_data + CMD_HEAD, strlen(want));

  /* Blank/whitespace-only CRLF line is not pushed */
  emit_vprintf("\x1b[0m\r\n");
  TEST_ASSERT_EQUAL_INT(1, s_log_sink.calls);

  TEST_ASSERT_NULL(data_matrix_disconnect(&lnk));
}

static void test_error_history_wraparound_push_and_relay_except_sender(void) {
  const data_matrix_port_id_t mod_err = DATA_MATRIX_PORT(DATA_MATRIX_PORT_MODULE, MODULE_ID_ERRORS);
  TEST_ASSERT_NULL(data_matrix_connect(&(data_matrix_link_t){mod_err, s_err_sink_a.port.id}));
  TEST_ASSERT_NULL(data_matrix_connect(&(data_matrix_link_t){mod_err, s_err_sink_b.port.id}));

  /* Report CONFIG_SYS_LOG_ERRORS_KEPT + 2 errors so the ring wraps by 2 */
  for (int i = 0; i < CONFIG_SYS_LOG_ERRORS_KEPT + 2; i++) {
    err_h e = ERR_NEW(ERR_RANGE, .value = i, .min = 0, .max = 100);
    sys_log_report(e, NULL);
    err_free(e);
  }
  TEST_ASSERT_EQUAL_INT(CONFIG_SYS_LOG_ERRORS_KEPT + 2, s_err_sink_a.calls);
  TEST_ASSERT_EQUAL_INT(CONFIG_SYS_LOG_ERRORS_KEPT + 2, s_err_sink_b.calls);
  TEST_ASSERT_EQUAL_HEX8(CMD_SEND_NOWAIT | CMD_SEND_URGENT, s_err_sink_a.last_flags);
  TEST_ASSERT_EQUAL_UINT8(CMD_PUSH, s_err_sink_a.last_data[0] >> 6);
  TEST_ASSERT_EQUAL_UINT8(MODULE_ID_ERRORS, s_err_sink_a.last_data[2]);

  /* OP_ERRORS: total == CONFIG_SYS_LOG_ERRORS_KEPT, index 0 is oldest surviving (value == 2) */
  uint8_t reply[1 + ERR_RECORD_MAX];
  uint32_t rlen = 0;
  sys_log_errors_args_t a = {.index = 0};
  TEST_ASSERT_NULL(route_log(CMD_DIR_READ, LOG_OP_ERRORS, &a, sizeof(a), reply, sizeof(reply), &rlen));
  TEST_ASSERT_EQUAL_UINT8(CONFIG_SYS_LOG_ERRORS_KEPT, reply[0]);
  TEST_ASSERT_GREATER_THAN_UINT32(1, rlen);
  err_h parsed = NULL;
  TEST_ASSERT_NULL(err_record_parse(reply + 1, rlen - 1u, &parsed));
  TEST_ASSERT_NOT_NULL(parsed);
  TEST_ASSERT_EQUAL_HEX32(ERR_RANGE, err_tag(parsed));
  TEST_ASSERT_EQUAL_INT32(2, ERR_DATA(parsed, ERR_RANGE)->value);
  err_free(parsed);

  /* Index past end returns [total] with 0 record bytes */
  a.index = CONFIG_SYS_LOG_ERRORS_KEPT;
  TEST_ASSERT_NULL(route_log(CMD_DIR_READ, LOG_OP_ERRORS, &a, sizeof(a), reply, sizeof(reply), &rlen));
  TEST_ASSERT_EQUAL_UINT32(1, rlen);
  TEST_ASSERT_EQUAL_UINT8(CONFIG_SYS_LOG_ERRORS_KEPT, reply[0]);

  /* Relay: a record arriving at MODULE_ID_ERRORS from sink_a is forwarded to sink_b, NOT back to sink_a */
  s_err_sink_a.calls = 0;
  s_err_sink_b.calls = 0;
  const uint8_t fwd[] = {0xDE, 0xAD, 0xBE, 0xEF};
  TEST_ASSERT_TRUE(data_matrix_send_to(&s_err_sink_a.port, mod_err, CMD_SEND_URGENT, fwd, sizeof(fwd)));
  TEST_ASSERT_EQUAL_INT(0, s_err_sink_a.calls);
  TEST_ASSERT_EQUAL_INT(1, s_err_sink_b.calls);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(fwd, s_err_sink_b.last_data, sizeof(fwd));

  TEST_ASSERT_NULL(data_matrix_disconnect(&(data_matrix_link_t){mod_err, s_err_sink_a.port.id}));
  TEST_ASSERT_NULL(data_matrix_disconnect(&(data_matrix_link_t){mod_err, s_err_sink_b.port.id}));
}

int main(void) {
  TEST_ASSERT_NULL(router_root_init());
  TEST_ASSERT_NULL(data_matrix_register(&s_log_sink.port));
  TEST_ASSERT_NULL(data_matrix_register(&s_err_sink_a.port));
  TEST_ASSERT_NULL(data_matrix_register(&s_err_sink_b.port));
  TEST_ASSERT_NULL(sys_log_init());
  /* Capture the hook installed by sys_log_init() without removing it */
  s_log_hook = esp_log_set_vprintf(NULL);
  esp_log_set_vprintf(s_log_hook);
  TEST_ASSERT_NOT_NULL(s_log_hook);

  UNITY_BEGIN();
  RUN_TEST(test_config_validation_and_router);
  RUN_TEST(test_log_vprintf_ansi_stripping_and_unlinked_skip);
  RUN_TEST(test_error_history_wraparound_push_and_relay_except_sender);
  return UNITY_END();
}
