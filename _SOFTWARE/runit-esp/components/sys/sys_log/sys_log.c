#include "sys_log.h"

#include "data_matrix.h"
#include "err_log.h"
#include "err_record.h"
#include "errors_id.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "sys_log_router.h"

ERR_MODULE_DEFINE(MODULE_ID_LOG, ERR_SYS_LOG_MAP)

#define ANSI_ESC  '\x1b'
#define LEVEL_MAX CONFIG_LOG_MAXIMUM_LEVEL

_Static_assert(CONFIG_LOG_DEFAULT_LEVEL <= LEVEL_MAX, "the default log level is above the compile-time ceiling");
_Static_assert(ERR_RECORD_MAX <= CMD_DATA_MAX, "an error record must fit a frame");

static struct {
  portMUX_TYPE lock;       // protects cfg and history
  sys_log_cfg_t cfg;
  vprintf_like_t console;  // what ESP_LOG wrote to before the hook
  struct {
    uint8_t bytes[ERR_RECORD_MAX];
    uint8_t len;
  } history[CONFIG_SYS_LOG_ERRORS_KEPT];
  uint32_t history_next;
  uint32_t history_count;
} s_log = {
    .lock = portMUX_INITIALIZER_UNLOCKED,
    .cfg = {.level = CONFIG_LOG_DEFAULT_LEVEL, .mirror_serial = 0, .trace_errors = 1},
};

// Removes ANSI colour sequences (ESC [ ... letter) and trailing CR/LF in place.
static size_t clean_line(char* s, size_t len) {
  size_t out = 0;
  for (size_t i = 0; i < len; i++) {
    if (s[i] == ANSI_ESC) {
      while (i < len && !((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z'))) {
        i++;
      }
      continue;  // skips the final letter too
    }
    s[out++] = s[i];
  }
  while (out > 0 && (s[out - 1] == '\n' || s[out - 1] == '\r')) {
    out--;
  }
  return out;
}

/* ----------------------------------------------------------------------------
 * The ports: the log lines out (LOGS), the error records out and in (ERRORS)
 * ------------------------------------------------------------------------- */

static __thread bool s_sending;  // this task is sending a line or a record: what it logs or reports meanwhile is dropped

// Another board's record: passed on to every port linked from ERRORS but the one it came from.
static void errors_in(data_matrix_port_t* self, data_matrix_port_id_t from, uint8_t flags, const uint8_t* data,
                      uint32_t len) {
  data_matrix_send_except(self, from, flags, data, len);
}

static data_matrix_port_t s_logs = {.id = DATA_MATRIX_PORT(DATA_MATRIX_PORT_MODULE, MODULE_ID_LOG)};
static data_matrix_port_t s_errors = {.id = DATA_MATRIX_PORT(DATA_MATRIX_PORT_MODULE, MODULE_ID_ERRORS),
                                      .in = errors_in};

// Sends `packet`, its data already after CMD_HEAD free bytes: writes [hdr PUSH, key] there, NOWAIT (a full line drops
// it: waiting or reporting it would loop).
static void push(data_matrix_port_t* port, uint8_t urgent, uint8_t* packet, uint32_t len) {
  const cmd_hdr_t hdr = {.type = (uint8_t)(CMD_TYPE(CMD_PUSH) | urgent), .key = (uint8_t)port->id};
  memcpy(packet, &hdr, CMD_HEAD);
  s_sending = true;
  data_matrix_send(port, (uint8_t)(CMD_SEND_NOWAIT | urgent), packet, CMD_HEAD + len);
  s_sending = false;
}

static sys_log_cfg_t read_cfg(void) {
  portENTER_CRITICAL(&s_log.lock);
  const sys_log_cfg_t cfg = s_log.cfg;
  portEXIT_CRITICAL(&s_log.lock);
  return cfg;
}

static int log_vprintf(const char* fmt, va_list args) {
  const sys_log_cfg_t cfg = read_cfg();
  va_list copy;
  va_copy(copy, args);
  const int ret = (cfg.mirror_serial && s_log.console != NULL) ? s_log.console(fmt, args) : 0;

  // Only from tasks, after the scheduler runs, not a line made while this task sends one (push), and only when LOGS is
  // linked: an unlinked port would drop it, so it is not even formatted.
  if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING && !xPortInIsrContext() && !s_sending &&
      data_matrix_linked(&s_logs)) {
    uint8_t packet[CMD_FRAME_MAX];  // the header, then the line: one frame
    char* line = (char*)packet + CMD_HEAD;
    const int n = vsnprintf(line, CMD_DATA_MAX, fmt, copy);
    if (n > 0) {
      const size_t len = clean_line(line, (size_t)n < CMD_DATA_MAX ? (size_t)n : CMD_DATA_MAX - 1u);
      if (len > 0) {
        push(&s_logs, 0, packet, (uint32_t)len);
      }
    }
  }
  va_end(copy);
  return ret;
}

static void apply(const sys_log_cfg_t* cfg) {
  err_log_enable(cfg->trace_errors != 0);
  esp_log_level_set("*", (esp_log_level_t)cfg->level);
}

err_h sys_log_init(void) {
  ERR_TRY(data_matrix_register(&s_logs));
  ERR_TRY(data_matrix_register(&s_errors));
  s_log.console = esp_log_set_vprintf(log_vprintf);
  const sys_log_cfg_t cfg = read_cfg();
  apply(&cfg);
  return NULL;
}

err_h sys_log_set(const sys_log_cfg_t* cfg) {
  ERR_CHECK(cfg != NULL, ERR_CMD_BAD_ARG);
  ERR_CHECK(cfg->level <= LEVEL_MAX, ERR_LOG_BAD_LEVEL, .level = cfg->level, .max = LEVEL_MAX);
  ERR_CHECK(cfg->mirror_serial <= 1 && cfg->trace_errors <= 1, ERR_CMD_BAD_ARG);
  portENTER_CRITICAL(&s_log.lock);
  s_log.cfg = *cfg;
  portEXIT_CRITICAL(&s_log.lock);
  apply(cfg);
  return NULL;
}

void sys_log_get(sys_log_cfg_t* out) {
  *out = read_cfg();
}

void sys_log_report(err_h chain, const cmd_req_t* cmd) {
  uint8_t packet[CMD_HEAD + ERR_RECORD_MAX];  // the header, then the record: one frame
  uint8_t* record = packet + CMD_HEAD;
  const uint32_t len = err_record_cmd(cmd, chain, record, ERR_RECORD_MAX);
  if (len == 0) {
    return;
  }
  portENTER_CRITICAL(&s_log.lock);
  memcpy(s_log.history[s_log.history_next].bytes, record, len);
  s_log.history[s_log.history_next].len = (uint8_t)len;
  s_log.history_next = (s_log.history_next + 1u) % CONFIG_SYS_LOG_ERRORS_KEPT;
  if (s_log.history_count < CONFIG_SYS_LOG_ERRORS_KEPT) {
    s_log.history_count++;
  }
  portEXIT_CRITICAL(&s_log.lock);
  if (!s_sending) {
    push(&s_errors, CMD_SEND_URGENT, packet, len);
  }
}

uint32_t sys_log_error_get(uint32_t index, uint8_t* out, uint32_t* total) {
  uint32_t len = 0;
  portENTER_CRITICAL(&s_log.lock);
  *total = s_log.history_count;
  if (index < s_log.history_count) {
    const uint32_t n = CONFIG_SYS_LOG_ERRORS_KEPT;
    const uint32_t at = (s_log.history_next + n - s_log.history_count + index) % n;  // index 0 = oldest
    len = s_log.history[at].len;
    memcpy(out, s_log.history[at].bytes, len);
  }
  portEXIT_CRITICAL(&s_log.lock);
  return len;
}
