#include "sys_error_log.h"
#include <esp_log.h>
#include <sdkconfig.h>
#include <stdarg.h>
#include <stdio.h>
#include "enc_sys_errors.h"
#include "utils.h"

#undef OWNER
#define OWNER OWNER_SYS_ERRORS_CONFIG

static const char* TAG = __FILE_NAME__;

static struct {
  bool            mirror_on_serial;
  bool            trace_errors;
  esp_log_level_t log_level;
} s_log_state = {
    .mirror_on_serial = true,
    .trace_errors     = true,
    .log_level        = ESP_LOG_INFO,
};

// Re-entrancy guard, not a lock: log send paths (e.g. transport send)
// may log on their own error paths, and that log would come straight back here.
// A nested call finds the flag set and takes the serial-only path instead of recursing.
static __thread bool s_in_log_sink;

static sys_error_sink_t s_sink;

/* Chains waiting for the log task, which logs, sends and releases them. */
R_QUEUE_DEFINE(s_log_queue, CONFIG_SYS_ERRORS_LOG_QUEUE_LEN, sizeof(err_h));
R_TASK_DEFINE(s_log_task, CONFIG_SYS_ERRORS_LOG_TASK_STACK_SIZE);
static uint32_t s_log_dropped;

/* Repeat suppression (log task only): a chain matching a recent one is
   counted instead of logged until its window ends. */
typedef struct {
  uint32_t signature; /* 0 = free slot */
  uint16_t tag;       /* outermost node, for the summary line */
  uint16_t owner;
  TickType_t since;
  uint32_t repeats;
} repeat_slot_t;

static repeat_slot_t s_repeat[CONFIG_SYS_ERRORS_REPEAT_SLOTS];

void SE_register_sink(const sys_error_sink_t* sink) {
  s_sink = sink ? *sink : (sys_error_sink_t){0};
}

static void sink_send_log(const void* data, size_t len) {
  if (s_sink.send_log) s_sink.send_log(data, len);
}

// -----------------------------------------------------------------------------
// Error Chain Expansion (Direct Connector Send)
// -----------------------------------------------------------------------------

void se_log_error_chain(err_h chain) {
  if (!s_log_state.trace_errors || chain == NULL || s_log_state.log_level < ESP_LOG_ERROR) {
    return;
  }

  char     desc[CONFIG_SYS_ERRORS_LOG_LINE_MAX];
  char     line[CONFIG_SYS_ERRORS_LOG_LINE_MAX];
  err_h nodes[SE_MAX_CHAIN_DEPTH];
  bool complete;
  size_t count = SE_collect_chain(chain, nodes, &complete);

  for (size_t depth = 0; depth < count; ++depth) {
    err_h node = nodes[depth];
    if (!SE_describe_payload(node->tag, node->payload, desc, sizeof(desc))) {
      size_t  payload_len = SE_get_payload_size(node->tag);
      uint8_t dump_len    = (payload_len < 32u) ? (uint8_t)payload_len : 32u;
      size_t  pos         = 0;
      for (uint8_t i = 0; i < dump_len && pos + 3 < sizeof(desc); i++) {
        pos += (size_t)snprintf(desc + pos, sizeof(desc) - pos, "%02X ", node->payload[i]);
      }
      desc[pos] = '\0';
    }
    int32_t len = snprintf(line, sizeof(line), "[%lu] owner=%s (0x%04lX) tag=%s (%ld): %s\n",
                       (long)depth, SE_get_owner_name(node->owner), (unsigned long)node->owner,
                       SE_get_tag_name(node->tag), (long)node->tag, desc);
    size_t out_len = 0;
    if (len > 0) {
      out_len = ((size_t)len < sizeof(line)) ? (size_t)len : sizeof(line) - 1u;
      sink_send_log(line, out_len);
    }
    if (s_log_state.mirror_on_serial) {
      bool previous = s_in_log_sink;
      s_in_log_sink = true;
      // ESP_LOGE ends the line itself: leave out the sink line's '\n'.
      int text_len = (out_len > 0 && line[out_len - 1] == '\n') ? (int)out_len - 1 : (int)out_len;
      ESP_LOGE(TAG, "%.*s", text_len, line);
      s_in_log_sink = previous;
    }
  }
  if (!complete) {
    const char warning[] = "<error chain truncated or corrupt>\n";
    sink_send_log(warning, sizeof(warning) - 1);
  }
}

// -----------------------------------------------------------------------------
// Logging Hook & Controls
// -----------------------------------------------------------------------------

#undef OWNER
#define OWNER OWNER_SYS_ERRORS_CONFIG

static int se_log_vprintf(const char* fmt, va_list args) {
  if (!s_in_log_sink) {
    s_in_log_sink = true;
    char    line[CONFIG_SYS_ERRORS_LOG_LINE_MAX];
    va_list rendered;
    va_copy(rendered, args);
    int32_t len = vsnprintf(line, sizeof(line), fmt, rendered);
    va_end(rendered);

    if (len > 0) {
      // vsnprintf reports what it *would* have written - clamp to what it did.
      size_t out_len = ((size_t)len < sizeof(line)) ? (size_t)len : sizeof(line) - 1u;
      sink_send_log(line, out_len);
    }
    s_in_log_sink = false;
  }

  // Mirror if selected
  if (s_log_state.mirror_on_serial) {
    return vprintf(fmt, args);
  }
  return 0;
}

/* FNV-1a over every node's tag and owner. */
static uint32_t chain_signature(err_h chain) {
  err_h nodes[SE_MAX_CHAIN_DEPTH];
  bool complete;
  size_t count = SE_collect_chain(chain, nodes, &complete);
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < count; ++i) {
    hash = (hash ^ (uint32_t)nodes[i]->tag) * 16777619u;
    hash = (hash ^ nodes[i]->owner) * 16777619u;
  }
  return hash ? hash : 1u;
}

static void repeat_flush(repeat_slot_t* slot) {
  if (slot->repeats > 0) {
    ESP_LOGW(TAG, "%s / %s repeated %lu more time(s)", SE_get_owner_name(slot->owner), SE_get_tag_name((err_tag_e)slot->tag),
             (unsigned long)slot->repeats);
  }
  slot->signature = 0;
  slot->repeats = 0;
}

/* True when chain repeats one logged within the window (it is then only counted). */
static bool repeat_suppressed(err_h chain, TickType_t now) {
  const TickType_t window = pdMS_TO_TICKS(CONFIG_SYS_ERRORS_REPEAT_WINDOW_MS);
  if (window == 0) return false;
  uint32_t signature = chain_signature(chain);
  repeat_slot_t* target = &s_repeat[0];
  for (size_t i = 0; i < CONFIG_SYS_ERRORS_REPEAT_SLOTS; ++i) {
    repeat_slot_t* slot = &s_repeat[i];
    if (slot->signature == signature) {
      if (now - slot->since < window) {
        slot->repeats++;
        return true;
      }
      target = slot;
      break;
    }
    if (slot->signature == 0 || (target->signature != 0 && slot->since < target->since)) target = slot;
  }
  repeat_flush(target);
  *target = (repeat_slot_t){.signature = signature, .tag = (uint16_t)chain->tag, .owner = (uint16_t)chain->owner, .since = now};
  return false;
}

/* Report and free slots whose window has ended. */
static void repeat_expire(TickType_t now) {
  const TickType_t window = pdMS_TO_TICKS(CONFIG_SYS_ERRORS_REPEAT_WINDOW_MS);
  for (size_t i = 0; i < CONFIG_SYS_ERRORS_REPEAT_SLOTS; ++i) {
    if (s_repeat[i].signature != 0 && now - s_repeat[i].since >= window) repeat_flush(&s_repeat[i]);
  }
}

static void se_log_task(void* arg) {
  (void)arg;
  err_h chain = NULL;
  const TickType_t wait = CONFIG_SYS_ERRORS_REPEAT_WINDOW_MS ? pdMS_TO_TICKS(CONFIG_SYS_ERRORS_REPEAT_WINDOW_MS) : portMAX_DELAY;
  while (1) {
    bool got = R_QUEUE_RECEIVE(s_log_queue, &chain, wait) == pdTRUE;
    TickType_t now = xTaskGetTickCount();
    repeat_expire(now);
    if (!got) continue;
    if (!repeat_suppressed(chain, now)) SE_release(SE_send(chain));
    SE_release(chain);
    uint32_t dropped = __atomic_exchange_n(&s_log_dropped, 0, __ATOMIC_RELAXED);
    if (dropped) {
      ESP_LOGW(TAG, "%lu error chain(s) dropped: log queue full", (unsigned long)dropped);
    }
  }
}

void SE_log(err_h chain) {
  if (chain == NULL) return;
  if (s_log_task == NULL) {
    SE_release(SE_send(chain));
    SE_release(chain);
    return;
  }
  if (R_QUEUE_SEND(s_log_queue, &chain, 0) != pdTRUE) {
    SE_release(chain);
    __atomic_add_fetch(&s_log_dropped, 1, __ATOMIC_RELAXED);
  }
}

void se_log_init(void) {
  esp_log_set_vprintf(se_log_vprintf);
  if (s_log_task == NULL) {
    R_TASK_START(s_log_task, se_log_task, NULL, CONFIG_SYS_ERRORS_LOG_TASK_PRIO);
  }
}

err_h SE_set_logging(esp_log_level_t level, bool mirror_serial, bool trace_errors) {
  SE_CHECK_IN_RANGE((uint32_t)level, 0, (uint32_t)ESP_LOG_VERBOSE);

  s_log_state.log_level        = level;
  s_log_state.mirror_on_serial = mirror_serial;
  s_log_state.trace_errors     = trace_errors;

  esp_log_level_set("*", level);
  esp_log_set_vprintf(se_log_vprintf);
  return NULL;
}

// -----------------------------------------------------------------------------
// Raw Packet Transmission (Direct Connector Send)
// -----------------------------------------------------------------------------

err_h SE_send_error_raw(err_h chain) {
  if (!chain) {
    return NULL;
  }

  if (!s_sink.send_packet) {
    return NULL;
  }

  uint8_t packet[CONFIG_SYS_ERRORS_PACKET_MAX];
  size_t max_len = s_sink.packet_max_len ? s_sink.packet_max_len() : 0;
  if (max_len == 0 || max_len > sizeof(packet)) {
    max_len = sizeof(packet);
  }

  size_t packet_len = 0;
  err_h  err        = enc_sys_errors_encode_chain(chain, packet, max_len, &packet_len);
  if (SE_IS_OK(err) && packet_len > 0) {
    s_sink.send_packet(packet, packet_len);
  }
  return err;
}

// -----------------------------------------------------------------------------
// Unified Error Dispatch
// -----------------------------------------------------------------------------

err_h SE_send(err_h chain) {
  if (!chain || s_in_log_sink) return NULL;
  s_in_log_sink = true;
  se_log_error_chain(chain);
  err_h result = SE_send_error_raw(chain);
  s_in_log_sink = false;
  return result;
}
