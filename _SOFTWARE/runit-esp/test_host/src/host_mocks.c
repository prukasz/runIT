#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>
#include "host_mocks.h"
#include "board_time.h"
#include "esp_err.h"

static pthread_mutex_t s_critical = PTHREAD_MUTEX_INITIALIZER;
void host_critical_enter(void) { pthread_mutex_lock(&s_critical); }
void host_critical_exit(void) { pthread_mutex_unlock(&s_critical); }

static pthread_mutex_t s_log_lock = PTHREAD_MUTEX_INITIALIZER;  /* tasks log too */
static uint64_t s_now_ms;
static bool s_real_time;
void host_time_set(uint64_t ms) { s_now_ms = ms; }
void host_time_real(bool on) { s_real_time = on; }
uint64_t board_time_now_ms(void) {
  if (!s_real_time) {
    return s_now_ms;
  }
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}
uint64_t board_time_uptime_ms(void) { return board_time_now_ms(); }

const char* esp_err_to_name(esp_err_t code) {
  switch (code) {
    case ESP_OK: return "ESP_OK";
    case ESP_FAIL: return "ESP_FAIL";
    case ESP_ERR_NO_MEM: return "ESP_ERR_NO_MEM";
    case ESP_ERR_INVALID_ARG: return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_INVALID_STATE: return "ESP_ERR_INVALID_STATE";
    default: return "ESP_ERR_?";
  }
}

#define LOG_MAX 64
#define LOG_LEN 320
static char s_lines[LOG_MAX][LOG_LEN];
static esp_log_level_t s_levels[LOG_MAX];
static int s_count;
static bool s_echo;
void host_log_echo(bool on) { s_echo = on; }

void host_log(esp_log_level_t level, const char* tag, const char* fmt, ...) {
  pthread_mutex_lock(&s_log_lock);
  if (s_count < LOG_MAX) {
    va_list a;
    va_start(a, fmt);
    vsnprintf(s_lines[s_count], LOG_LEN, fmt, a);
    va_end(a);
    s_levels[s_count++] = level;
  }
  if (s_echo) {
    static const char k_letter[] = "NEWIDV";
    va_list a;
    va_start(a, fmt);
    fprintf(stderr, "%c %s: ", k_letter[level <= ESP_LOG_VERBOSE ? level : 0], tag);
    vfprintf(stderr, fmt, a);
    fputc(10, stderr);
    va_end(a);
  }
  pthread_mutex_unlock(&s_log_lock);
}
void host_log_clear(void) { s_count = 0; }
int host_log_count(void) { return s_count; }
const char* host_log_line(int i) { return s_lines[i]; }
esp_log_level_t host_log_level(int i) { return s_levels[i]; }
