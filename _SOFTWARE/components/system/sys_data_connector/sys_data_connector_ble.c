#include "sys_data_connector_ble.h"
#include "sys_ble.h"

static void ble_provider_send(void* arg, const void* data, size_t len) {
  SE_release(sys_ble_char_send(SYS_DATA_BLE_ARG_CHAR(arg), data, len, true));
}

static SE_MUST_USE err_h ble_provider_dequeue(void* arg, uint8_t* buf, size_t max_len, size_t* out_len) {
  return sys_ble_char_rx_dequeue(SYS_DATA_BLE_ARG_CHAR(arg), buf, max_len, out_len);
}

/* Read data_present at wake time: sys_data_connector_set_wake_sem() can
   replace it after the bind. */
static void ble_provider_wake(void* ctx) {
  sys_data_connector_t* conn = (sys_data_connector_t*)ctx;
  if (conn->data_present) {
    xSemaphoreGive(conn->data_present);
  }
}

static SE_MUST_USE err_h ble_provider_bind_rx(void* arg, sys_data_connector_t* conn) {
  return sys_ble_char_link_rx_wake(SYS_DATA_BLE_ARG_CHAR(arg), ble_provider_wake, conn);
}

static SE_MUST_USE err_h ble_provider_unbind_rx(void* arg, sys_data_connector_t* conn) {
  return sys_ble_char_unlink_rx_wake(SYS_DATA_BLE_ARG_CHAR(arg), ble_provider_wake, conn);
}

err_h sys_data_connector_register_ble_provider(void) {
  static const sys_data_provider_driver_t driver = {
      .provider_id = SYS_DATA_PROVIDER_BLE,
      .name        = "BLE",
      .send        = ble_provider_send,
      .dequeue     = ble_provider_dequeue,
      .bind_rx     = ble_provider_bind_rx,
      .unbind_rx   = ble_provider_unbind_rx,
  };
  return sys_data_connector_register_provider(&driver);
}
