#include "selftest_harness.h"
#include "enc_sys_errors.h"
#include "runit_board_defs.h"
#include "sys_error.h"
#include "sys_error_config.h"
#include "sys_error_log.h"
#include "sys_data_connector.h"
#include "sys_data_connector_ble.h"

#define TEST_DEV_ID 42
#define TEST_ESP_CODE 0x1234
#define TEST_TX_CAP 128
#define TEST_PROVIDER_ID 7

typedef struct {
  uint32_t send_calls;
  size_t last_len;
  uint8_t last_packet[TEST_TX_CAP];
} error_tx_ctx_t;

static error_tx_ctx_t s_ctx;

static void error_test_provider_send(void* arg, const void* data, size_t len) {
  error_tx_ctx_t* c = (error_tx_ctx_t*)arg;
  c->send_calls++;
  c->last_len = len > TEST_TX_CAP ? TEST_TX_CAP : len;
  memcpy(c->last_packet, data, c->last_len);
}

static const sys_data_provider_driver_t s_error_test_driver = {
    .provider_id = TEST_PROVIDER_ID,
    .name        = "test_errors",
    .send        = error_test_provider_send,
};

// Reads one node record at *off, advancing it past the record.
static bool read_node(const uint8_t* pkt, size_t len, size_t* off, uint16_t* out_tag, const uint8_t** out_payload) {
  if (*off + ENC_SYS_ERRORS_NODE_HDR_LEN > len) return false;
  uint8_t payload_len = pkt[*off];
  *out_tag = (uint16_t)(pkt[*off + 1] | ((uint16_t)pkt[*off + 2] << 8));
  *out_payload = &pkt[*off + ENC_SYS_ERRORS_NODE_HDR_LEN];
  *off += ENC_SYS_ERRORS_NODE_HDR_LEN + payload_len;
  return *off <= len;
}

void test_sys_error_ownership(void) {
  ESP_LOGI(TAG, "--- sys_error queue stable ownership tests ---");

  SE_clear_dropped_count();
  ck("dropped count initializes to 0", SE_get_dropped_count() == 0);

  // Register selftest provider and bind it to CONN_ID_ERRORS
  sys_data_connector_register_provider(&s_error_test_driver);
  sys_data_connector_t* err_conn = sys_data_connector_get(CONN_ID_ERRORS);
  memset(&s_ctx, 0, sizeof(s_ctx));
  sys_data_connector_bind_tx(err_conn, TEST_PROVIDER_ID, &s_ctx);

  // 1. Build an error chain: ERR_ESP_ERR <- ERR_DEV_DEP_FAILED <- ERR_DEP_FAILED
  err_h leaf = SE_ERR_NEW(ERR_ESP_ERR, .esp_code = TEST_ESP_CODE);
  err_h dev = SE_WRAP_DEV_ERR(leaf, TEST_DEV_ID);
  dev->owner = OWNER_DEVICE_BASE;
  err_h top = SE_WRAP_ERR(dev, ERR_DEP_FAILED, 0);

  // Dispatch error chain synchronously via unified SE_send
  err_h send_err = SE_send(top);
  ck("SE_send succeeded", send_err == NULL);
  ck("chain reached the errors connector", s_ctx.send_calls >= 1);

  // 2. Walk the encoded packet back apart - same layout the client rebuilds from
  bool header_ok = s_ctx.last_len >= ENC_SYS_ERRORS_HDR_LEN && s_ctx.last_packet[0] == ENC_SYS_ERRORS_FMT_VERSION &&
                   s_ctx.last_packet[1] == 3 && s_ctx.last_packet[2] == 3;
  ck("packet header reports 3 of 3 nodes", header_ok);

  size_t off = ENC_SYS_ERRORS_HDR_LEN;
  uint16_t tags[3] = {0};
  const uint8_t* payloads[3] = {NULL};
  bool walk_ok = header_ok;
  for (int i = 0; i < 3 && walk_ok; i++) {
    walk_ok = read_node(s_ctx.last_packet, s_ctx.last_len, &off, &tags[i], &payloads[i]);
  }
  ck("every node record is within the packet", walk_ok);
  ck("chain order survived encoding", walk_ok && tags[0] == ERR_DEP_FAILED && tags[1] == ERR_DEV_DEP_FAILED && tags[2] == ERR_ESP_ERR);

  /* Records sit back-to-back on the wire, so a payload is not necessarily
     aligned for its struct - copy it out before reading any field. */
  err_payload_ERR_DEV_DEP_FAILED_t dev_payload = {0};
  err_payload_ERR_ESP_ERR_t esp_payload = {0};
  if (walk_ok) {
    memcpy(&dev_payload, payloads[1], sizeof(dev_payload));
    memcpy(&esp_payload, payloads[2], sizeof(esp_payload));
  }
  ck("dispatched dev_id is intact", walk_ok && dev_payload.dev_id == TEST_DEV_ID);
  ck("leaf payload matches original esp_code", walk_ok && esp_payload.esp_code == TEST_ESP_CODE);

  // Unbind selftest provider from CONN_ID_ERRORS
  sys_data_connector_unbind_tx(err_conn, TEST_PROVIDER_ID);
}
