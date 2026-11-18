

/**
 * @file err_log.c
 * @brief A chain as log lines (err_log.h): one line per node, formatted into a stack buffer of
 * CONFIG_ERRORS_LOG_LINE_LEN bytes, at the chain's level.
 */

#include "err_log.h"

#include <stdarg.h>
#include <stdio.h>
#include "err_internal.h"
#include "esp_log.h"

#define TAG          __FILE_NAME__
#define LOG_LINE_LEN CONFIG_ERRORS_LOG_LINE_LEN

static volatile bool s_enabled = true;

// The names of err_sev_e, by value.
static const char* const k_sev_names[] = {"NONE", "LOW", "MEDIUM", "HIGH", "CRITICAL"};

// Appends to line at *pos, never past LOG_LINE_LEN; output is cut when it is full.
static void append(char* line, size_t* pos, const char* fmt, ...) {
  if (*pos >= LOG_LINE_LEN - 1) {
    return;
  }
  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(line + *pos, LOG_LINE_LEN - *pos, fmt, args);
  va_end(args);
  if (n > 0) {
    *pos += (size_t)n < LOG_LINE_LEN - *pos ? (size_t)n : LOG_LINE_LEN - 1 - *pos;
  }
}

// The name of a severity, "?" past the known ones.
static const char* sev_name(uint8_t sev) {
  return sev < sizeof(k_sev_names) / sizeof(k_sev_names[0]) ? k_sev_names[sev] : "?";
}

// One node as one line: [index] NAME (MODULE.code) SEV, the payload, the call site.
static void format_node(char* line, uint32_t index, err_h e) {
  size_t pos = 0;
  const err_tag_info_t* info = err_tag_info(e->tag);
  const err_module_t* module = err_module(ERR_MODULE(e->tag));

  append(line, &pos, "[%lu] ", (unsigned long)index);
  if (info != NULL) {
    append(line, &pos, "%s (%s.%u) %s", info->name, module->name, ERR_CODE(e->tag), sev_name(info->sev));
  } else {
    append(line, &pos, "unknown tag 0x%08lx", (unsigned long)e->tag);
  }
  // Payload: core ESP-IDF errors by name, everything else in hex.
  if (e->tag == ERR_ESP_ERR) {
    esp_err_t code = ERR_DATA(e, ERR_ESP_ERR)->code;
    append(line, &pos, " %s (0x%x)", esp_err_to_name(code), code);
  } else if (info != NULL && info->size > 0) {
    append(line, &pos, " data:");
    for (uint8_t i = 0; i < info->size; i++) {
      append(line, &pos, " %02x", e->data[i]);
    }
  }

#if CONFIG_ERRORS_INFO
  if (e->info != NULL) {
    append(line, &pos, " @ %s:%lu %s()", e->info->file, (unsigned long)e->info->line, e->info->func);
  }
#endif
}

void err_log(err_h chain) {
  err_log_ctx(chain, NULL);
}

void err_log_enable(bool on) {
  s_enabled = on;
}

void err_log_ctx(err_h chain, const char* context) {
  if (chain == NULL || !s_enabled) {
    return;
  }

  uint8_t sev = err_chain_sev(chain);
  esp_log_level_t level = sev >= ERR_SEV_HIGH ? ESP_LOG_ERROR : sev == ERR_SEV_MEDIUM ? ESP_LOG_WARN : ESP_LOG_INFO;
  if (context != NULL) {
    ESP_LOG_LEVEL(level, TAG, "%s", context);
  }

  char line[LOG_LINE_LEN];
  uint32_t index = 0;
  uint32_t left = ERR_CHAIN_MAX;
  err_h e = chain;
  for (; e != NULL && left > 0; e = e->next, left--) {
    format_node(line, index++, e);
    ESP_LOG_LEVEL(level, TAG, "%s", line);
  }
  if (e != NULL) {
    ESP_LOG_LEVEL(level, TAG, "chain longer than the pool - corrupted (cycle?), stopped");
  }
}
