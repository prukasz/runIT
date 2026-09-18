#include "sys_error_log.h"
#include <esp_log.h>
#include <stdarg.h>
#include <stdio.h>
#include "enc_sys_errors.h"
#include "sys_data_connector.h"
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

// -----------------------------------------------------------------------------
// Error Chain Expansion (Direct Connector Send)
// -----------------------------------------------------------------------------

void se_log_error_chain(err_h chain) {
  if (!s_log_state.trace_errors || chain == NULL || s_log_state.log_level < ESP_LOG_ERROR) {
    return;
  }

  char     desc[SE_LOG_LINE_MAX];
  char     line[SE_LOG_LINE_MAX];
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
    int len = snprintf(line, sizeof(line), "[%u] owner=%s (0x%04X) tag=%s (%d): %s\n",
                       (unsigned)depth, SE_get_owner_name(node->owner), (unsigned)node->owner,
                       SE_get_tag_name(node->tag), (int)node->tag, desc);
    if (len > 0) {
      size_t out_len = ((size_t)len < sizeof(line)) ? (size_t)len : sizeof(line) - 1u;
      sys_data_connector_send(sys_data_connector_get(SE_CONNECTOR_ID_LOGS), line, out_len);
    }
    if (s_log_state.mirror_on_serial) {
      bool previous = s_in_log_sink;
      s_in_log_sink = true;
      ESP_LOGE(TAG, "%s", line);
      s_in_log_sink = previous;
    }
  }
  if (!complete) {
    const char warning[] = "<error chain truncated or corrupt>\n";
    sys_data_connector_send(sys_data_connector_get(SE_CONNECTOR_ID_LOGS), warning, sizeof(warning) - 1);
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
    char    line[SE_LOG_LINE_MAX];
    va_list rendered;
    va_copy(rendered, args);
    int len = vsnprintf(line, sizeof(line), fmt, rendered);
    va_end(rendered);

    if (len > 0) {
      // vsnprintf reports what it *would* have written - clamp to what it did.
      size_t out_len = ((size_t)len < sizeof(line)) ? (size_t)len : sizeof(line) - 1u;
      sys_data_connector_send(sys_data_connector_get(SE_CONNECTOR_ID_LOGS), line, out_len);
    }
    s_in_log_sink = false;
  }

  // Mirror if selected
  if (s_log_state.mirror_on_serial) {
    return vprintf(fmt, args);
  }
  return 0;
}

void se_log_init(void) {
  esp_log_set_vprintf(se_log_vprintf);
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

  sys_data_connector_t* conn = sys_data_connector_get(SE_CONNECTOR_ID_ERRORS);
  size_t max_len = sys_data_connector_get_max_len(conn);
  uint8_t packet[SE_ERR_PACKET_MAX];
  if (max_len > sizeof(packet)) {
    max_len = sizeof(packet);
  }

  size_t packet_len = 0;
  err_h  err        = enc_sys_errors_encode_chain(chain, packet, max_len, &packet_len);
  if (SE_IS_OK(err) && packet_len > 0) {
    sys_data_connector_send(conn, packet, packet_len);
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
