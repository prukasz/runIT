#pragma once
#include <stdarg.h>
/* Host stand-in for esp_log.h: a spy. Every line goes to a buffer the test reads (host_mocks.h), and to stderr when
 * host_log_echo is on. */
typedef enum { ESP_LOG_NONE, ESP_LOG_ERROR, ESP_LOG_WARN, ESP_LOG_INFO, ESP_LOG_DEBUG, ESP_LOG_VERBOSE } esp_log_level_t;
typedef int (*vprintf_like_t)(const char*, va_list);
vprintf_like_t esp_log_set_vprintf(vprintf_like_t fn);
void esp_log_level_set(const char* tag, esp_log_level_t level);
esp_log_level_t host_log_level_get(void);
void host_log(esp_log_level_t level, const char* tag, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
#define ESP_LOG_LEVEL(level, tag, fmt, ...) host_log((level), (tag), fmt, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...)             host_log(ESP_LOG_ERROR, (tag), fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...)             host_log(ESP_LOG_WARN, (tag), fmt, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...)             host_log(ESP_LOG_INFO, (tag), fmt, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...)             host_log(ESP_LOG_DEBUG, (tag), fmt, ##__VA_ARGS__)
