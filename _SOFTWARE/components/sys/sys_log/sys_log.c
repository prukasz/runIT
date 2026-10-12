#include "sys_log.h"

#include "esp_log.h"
#include "sdkconfig.h"
#include "sys_connector.h"

ERR_MODULE_DEFINE(ERR_MOD_SYS_LOG, "log", ERR_SYS_LOG_MAP)

#define ANSI_ESC '\x1b'
#define LEVEL_MAX CONFIG_LOG_MAXIMUM_LEVEL

_Static_assert(CONFIG_LOG_DEFAULT_LEVEL <= LEVEL_MAX, "the default log level is above the compile-time ceiling");

#define DEFAULTS {.level = CONFIG_LOG_DEFAULT_LEVEL, .mirror_serial = 1, .trace_errors = 1, .send_errors = 1, .send_logs = 1}

static const sys_log_cfg_t k_defaults = DEFAULTS;

static sys_log_cfg_t s_cfg = DEFAULTS;
static volatile bool s_mirror = true;       // read by every log call
static volatile bool s_send_logs = true;    // read by every log call
static volatile bool s_send_errors = true;  // read by every reported error
static vprintf_like_t s_console;       // what ESP_LOG wrote to before the hook
static uint8_t s_conn;
static bool s_hooked;

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
  const int ret = s_mirror ? s_console(fmt, args) : 0;

  /* Only from tasks (not the connector task: its own logs would loop), and
   * after the scheduler runs. A full queue just drops the line. */
  if (s_send_logs && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING && !xPortInIsrContext() && !sys_connector_is_task()) {
    char line[CONFIG_SYS_LOG_LINE_MAX];
    const int n = vsnprintf(line, sizeof(line), fmt, copy);
    if (n > 0) {
      size_t len = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1;
      len = clean_line(line, len);
      if (len > 0) {
        err_free(sys_connector_send(s_conn, line, (uint32_t)len));  // dropped, not reported: would log
      }
    }
  }
  va_end(copy);
  return ret;
}

static err_h check_level(uint8_t level) {
  ERR_CHECK(level <= LEVEL_MAX, ERR_LOG_BAD_LEVEL, .level = level, .max = LEVEL_MAX);
  return NULL;
}

static void apply(const sys_log_cfg_t* cfg) {
  s_cfg = *cfg;
  s_mirror = cfg->mirror_serial != 0;
  s_send_logs = cfg->send_logs != 0;
  s_send_errors = cfg->send_errors != 0;
  err_log_enable(cfg->trace_errors != 0);
  esp_log_level_set("*", (esp_log_level_t)cfg->level);
}

err_h sys_log_init(uint8_t connector_id) {
  sys_connector_info_t info;
  ERR_TRY(sys_connector_info(connector_id, &info));  // exists
  s_conn = connector_id;
  if (!s_hooked) {
    s_console = esp_log_set_vprintf(log_vprintf);
    s_hooked = true;
  }
  apply(&k_defaults);  // the stored options (SYS_ACTIONS_ID_LOG) are applied by whoever starts the project
  return NULL;
}

err_h sys_log_set(const sys_log_cfg_t* cfg) {
  ERR_CHECK(cfg != NULL, ERR_CMD_BAD_ARG);
  ERR_TRY(check_level(cfg->level));
  const sys_log_cfg_t c = {.level = cfg->level,
                           .mirror_serial = cfg->mirror_serial != 0,
                           .trace_errors = cfg->trace_errors != 0,
                           .send_errors = cfg->send_errors != 0,
                           .send_logs = cfg->send_logs != 0};
  apply(&c);
  return NULL;
}

void sys_log_get(sys_log_cfg_t* out) {
  *out = s_cfg;
}

bool sys_log_send_errors(void) {
  return s_send_errors;
}
