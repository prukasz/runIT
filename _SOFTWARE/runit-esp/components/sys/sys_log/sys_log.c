#include "sys_log.h"

#include "esp_log.h"
#include "sdkconfig.h"
#include "sys_connector.h"

ERR_MODULE_DEFINE(ERR_MOD_SYS_LOG, "log", ERR_SYS_LOG_MAP)

#define ANSI_ESC '\x1b'
#define LEVEL_MAX CONFIG_LOG_MAXIMUM_LEVEL

_Static_assert(CONFIG_LOG_DEFAULT_LEVEL <= LEVEL_MAX, "the default log level is above the compile-time ceiling");

// The state, zero at the start (.bss).
static struct {
  vprintf_like_t console;  // what ESP_LOG wrote to before the hook
  uint8_t conn;            // the logs connector
  /* The error history: a ring of the latest records, written by whoever reports (any task) and read by the router.
   * A critical section (s_log_preset.history_lock) keeps a record whole; it holds a copy of at most
   * SYS_LOG_RECORD_MAX bytes, never a report. */
  struct {
    uint8_t bytes[SYS_LOG_RECORD_MAX];
    uint8_t len;
  } history[CONFIG_SYS_LOG_ERRORS_KEPT];
  uint32_t history_next;   // where the next one goes
  uint32_t history_count;  // kept
} s_log;

// What starts non-zero (.data, kept small).
static struct {
  sys_log_cfg_t cfg;  // the options in force, the defaults until a SET; read by every log call and reported error
  portMUX_TYPE history_lock;
} s_log_preset = {
    .cfg = {.level = CONFIG_LOG_DEFAULT_LEVEL, .mirror_serial = 1, .trace_errors = 1, .send_errors = 1, .send_logs = 1},
    .history_lock = portMUX_INITIALIZER_UNLOCKED,
};

// Removes ANSI colour sequences (ESC [ ... letter) and the line end, in place.
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

static int log_vprintf(const char* fmt, va_list args) {
  va_list copy;
  va_copy(copy, args);
  const int ret = s_log_preset.cfg.mirror_serial ? s_log.console(fmt, args) : 0;

  /* Only from tasks (not the connector task: its own logs would loop), and
   * after the scheduler runs. A full queue just drops the line. */
  if (s_log_preset.cfg.send_logs && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING && !xPortInIsrContext() &&
      !sys_connector_is_task()) {
    char line[CONFIG_SYS_LOG_LINE_MAX];
    const int n = vsnprintf(line, sizeof(line), fmt, copy);
    if (n > 0) {
      size_t len = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1;
      len = clean_line(line, len);
      if (len > 0) {
        err_free(sys_connector_send(s_log.conn, line, (uint32_t)len));  // dropped, not reported: would log
      }
    }
  }
  va_end(copy);
  return ret;
}

static void apply(void) {
  err_log_enable(s_log_preset.cfg.trace_errors);
  esp_log_level_set("*", (esp_log_level_t)s_log_preset.cfg.level);
}

err_h sys_log_init(uint8_t connector_id) {
  sys_connector_info_t info;
  ERR_TRY(sys_connector_info(connector_id, &info));  // exists
  s_log.conn = connector_id;
  if (s_log.console == NULL) {
    s_log.console = esp_log_set_vprintf(log_vprintf);
  }
  apply();  // the defaults; the stored options (SYS_ACTIONS_ID_LOG) are applied by whoever starts the project
  return NULL;
}

err_h sys_log_set(const sys_log_cfg_t* cfg) {
  ERR_CHECK(cfg != NULL, ERR_CMD_BAD_ARG);
  ERR_CHECK(cfg->level <= LEVEL_MAX, ERR_LOG_BAD_LEVEL, .level = cfg->level, .max = LEVEL_MAX);
  ERR_CHECK(cfg->mirror_serial <= 1 && cfg->trace_errors <= 1 && cfg->send_errors <= 1 && cfg->send_logs <= 1,
            ERR_CMD_BAD_ARG);
  s_log_preset.cfg = *cfg;
  apply();
  return NULL;
}

void sys_log_get(sys_log_cfg_t* out) {
  *out = s_log_preset.cfg;
}

void sys_log_error(const uint8_t* record, uint32_t len) {
  if (record == NULL || len == 0 || len > SYS_LOG_RECORD_MAX) {
    return;
  }
  portENTER_CRITICAL(&s_log_preset.history_lock);
  memcpy(s_log.history[s_log.history_next].bytes, record, len);
  s_log.history[s_log.history_next].len = (uint8_t)len;
  s_log.history_next = (s_log.history_next + 1) % CONFIG_SYS_LOG_ERRORS_KEPT;
  if (s_log.history_count < CONFIG_SYS_LOG_ERRORS_KEPT) {
    s_log.history_count++;
  }
  portEXIT_CRITICAL(&s_log_preset.history_lock);
  if (s_log_preset.cfg.send_errors) {
    err_free(sys_connector_send(SYS_CONNECTOR_ERRORS, record, len));  // a full queue drops it: reporting would loop
  }
}

uint32_t sys_log_error_get(uint32_t index, uint8_t* out, uint32_t* total) {
  uint32_t len = 0;
  portENTER_CRITICAL(&s_log_preset.history_lock);
  *total = s_log.history_count;
  if (index < s_log.history_count) {
    const uint32_t n = CONFIG_SYS_LOG_ERRORS_KEPT;
    const uint32_t at = (s_log.history_next + n - s_log.history_count + index) % n;  // index 0 = the oldest
    len = s_log.history[at].len;
    memcpy(out, s_log.history[at].bytes, len);
  }
  portEXIT_CRITICAL(&s_log_preset.history_lock);
  return len;
}
