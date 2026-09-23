#include "sys_ble_provider.h"
#include "sys_ble.h"
#include "sys_data_connector.h"

#define OWNER OWNER_SYS_BLE_PROVIDER

/* Endpoints come from settings packets as 32-bit values; a BLE one is a UUID. */
static SE_MUST_USE err_h check_uuid(uint32_t endpoint) {
  if (endpoint > UINT16_MAX) {
    SE_FAIL(ERR_INVALID_VAL_UI32, .val = endpoint, .min = 0, .max = UINT16_MAX);
  }
  return NULL;
}

static SE_MUST_USE err_h ble_provider_send(uint32_t endpoint, uint32_t peer, const uint8_t* frame, size_t len) {
  (void)peer;
  SE_TRY(check_uuid(endpoint));
  SE_TRY(sys_ble_char_send((uint16_t)endpoint, frame, len, true));
  return NULL;
}

static SE_MUST_USE err_h ble_provider_dequeue(uint32_t endpoint, uint8_t* buf, size_t max_len, size_t* out_len, uint32_t* out_peer) {
  *out_peer = 0;
  SE_TRY(check_uuid(endpoint));
  SE_TRY(sys_ble_char_rx_dequeue((uint16_t)endpoint, buf, max_len, out_len));
  return NULL;
}

static size_t ble_provider_max_frame(uint32_t endpoint, uint32_t peer) {
  (void)peer;
  return endpoint > UINT16_MAX ? 0 : sys_ble_char_max_payload((uint16_t)endpoint);
}

static void ble_provider_wake(void* ctx) {
  sys_data_connector_notify_rx((uint8_t)(uintptr_t)ctx);
}

static SE_MUST_USE err_h ble_provider_bind_rx(uint32_t endpoint, uint8_t conn_id) {
  SE_TRY(check_uuid(endpoint));
  SE_TRY(sys_ble_char_link_rx_wake((uint16_t)endpoint, ble_provider_wake, (void*)(uintptr_t)conn_id));
  return NULL;
}

static SE_MUST_USE err_h ble_provider_unbind_rx(uint32_t endpoint, uint8_t conn_id) {
  SE_TRY(check_uuid(endpoint));
  SE_TRY(sys_ble_char_unlink_rx_wake((uint16_t)endpoint, ble_provider_wake, (void*)(uintptr_t)conn_id));
  return NULL;
}

err_h sys_ble_provider_register(uint8_t provider_id) {
  static const sys_data_connector_provider_t s_ble_provider = {
      .name = "BLE",
      .send = ble_provider_send,
      .dequeue = ble_provider_dequeue,
      .max_frame = ble_provider_max_frame,
      .bind_rx = ble_provider_bind_rx,
      .unbind_rx = ble_provider_unbind_rx,
  };
  SE_TRY(sys_data_connector_register_provider(provider_id, &s_ble_provider));
  return NULL;
}
