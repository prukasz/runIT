#include "board_time.h"

#include <sys/time.h>

#include "esp_timer.h"

uint64_t board_time_now_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec * 1000u + (uint64_t)tv.tv_usec / 1000u;
}

bool board_time_is_set(void) {
  return board_time_now_ms() >= BOARD_TIME_SET_AFTER_MS;
}

uint64_t board_time_unix_ms(void) {
  const uint64_t now = board_time_now_ms();
  return now >= BOARD_TIME_SET_AFTER_MS ? now : 0;
}

bool board_time_set(uint64_t unix_ms) {
  const struct timeval tv = {.tv_sec = (time_t)(unix_ms / 1000u), .tv_usec = (suseconds_t)((unix_ms % 1000u) * 1000u)};
  return settimeofday(&tv, NULL) == 0;
}

uint64_t board_time_uptime_ms(void) {
  return (uint64_t)(esp_timer_get_time() / 1000);
}

uint64_t board_time_at_uptime_ms(uint64_t uptime_ms) {
  const uint64_t ago = board_time_uptime_ms() - uptime_ms;
  const uint64_t now = board_time_now_ms();
  return now > ago ? now - ago : 0;
}
