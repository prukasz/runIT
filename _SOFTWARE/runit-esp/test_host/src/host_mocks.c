#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include "host_mocks.h"
#include "board_time.h"
#include "esp_err.h"

static pthread_mutex_t s_critical = PTHREAD_MUTEX_INITIALIZER;
void host_critical_enter(void) { pthread_mutex_lock(&s_critical); }
void host_critical_exit(void) { pthread_mutex_unlock(&s_critical); }

static pthread_mutex_t s_log_lock = PTHREAD_MUTEX_INITIALIZER;  /* tasks log too */
static uint64_t s_now_ms;
void host_time_set(uint64_t ms) { s_now_ms = ms; }
uint64_t board_time_now_ms(void) { return s_now_ms; }

const char* esp_err_to_name(esp_err_t code) {
  return code == ESP_OK ? "ESP_OK" : code == ESP_FAIL ? "ESP_FAIL" : "ESP_ERR_?";
}

#define LOG_MAX 64
#define LOG_LEN 320
static char s_lines[LOG_MAX][LOG_LEN];
static esp_log_level_t s_levels[LOG_MAX];
static int s_count;

void host_log(esp_log_level_t level, const char* tag, const char* fmt, ...) {
  (void)tag;
  pthread_mutex_lock(&s_log_lock);
  if (s_count < LOG_MAX) {
    va_list a;
    va_start(a, fmt);
    vsnprintf(s_lines[s_count], LOG_LEN, fmt, a);
    va_end(a);
    s_levels[s_count++] = level;
  }
  pthread_mutex_unlock(&s_log_lock);
}
void host_log_clear(void) { s_count = 0; }
int host_log_count(void) { return s_count; }
const char* host_log_line(int i) { return s_lines[i]; }
esp_log_level_t host_log_level(int i) { return s_levels[i]; }
