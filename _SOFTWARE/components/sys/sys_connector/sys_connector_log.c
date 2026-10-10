#include <stdarg.h>
#include <stdio.h>
#include "esp_log.h"
#include "sdkconfig.h"
#include "sys_connector.h"
#include "sys_connector_priv.h"
#include "utils.h"

#define ANSI_ESC '\x1b'

static vprintf_like_t s_console;  // what ESP_LOG wrote to before the capture
static uint8_t s_log_conn;
static bool s_capturing;

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
  int ret = s_console(fmt, args);

  /* Only from tasks (not the connector task: its own logs would loop), and
   * after the scheduler runs. A full queue just drops the line. */
  if (s_capturing && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING && !xPortInIsrContext() &&
      !sys_connector_is_task()) {
    char line[CONFIG_SYS_CONNECTOR_LOG_LINE_MAX];
    int n = vsnprintf(line, sizeof(line), fmt, copy);
    if (n > 0) {
      size_t len = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1;
      len = clean_line(line, len);
      if (len > 0) {
        err_free(sys_connector_send(s_log_conn, line, (uint32_t)len));  // dropped, not reported: would log
      }
    }
  }
  va_end(copy);
  return ret;
}

err_h sys_connector_capture_logs(uint8_t id) {
  sys_connector_info_t info;
  ERR_TRY(sys_connector_info(id, &info));  // exists
  s_log_conn = id;
  if (!s_capturing) {
    s_console = esp_log_set_vprintf(log_vprintf);
    s_capturing = true;
  }
  return NULL;
}
