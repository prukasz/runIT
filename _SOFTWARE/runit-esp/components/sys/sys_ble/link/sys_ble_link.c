#include "sys_ble_link.h"

#include "router.h"
#include "sdkconfig.h"
#include "sys_ble.h"
#include "sys_ble_priv.h"
#include "sys_connector.h"

#define LINK_SEND_WAIT_MS     100u     // answer waits this long for TX buffer space
#define PROVIDER_SEND_WAIT_MS 20u  // stream frame (connector task) waits this long
#define TX_FRAMES             4u            // frames the TX / LOGS buffers hold
#define STATUS_BUFFER         512u      // size of the STATUS characteristic buffer

_Static_assert(CONFIG_SYS_BLE_TX_ITEM_MAX >= CONFIG_ROUTER_FRAME_MAX,
               "SYS_BLE_TX_ITEM_MAX must hold a whole router frame");

/* ----------------------------------------------------------------------------
 * RX: framed packets -> one frame -> router_submit (host task only)
 * ------------------------------------------------------------------------- */

static struct {
  uint8_t buf[CONFIG_ROUTER_FRAME_MAX];
  size_t len;
  uint8_t expected;  // counter of the next packet
  bool active;       // inside a frame
} s_rx;

static void rx_packet(void* ctx, const uint8_t* data, size_t len) {
  (void)ctx;
  uint8_t hdr = data[0];
  uint8_t count = hdr & SYS_BLE_FRAG_COUNT_MASK;
  if (hdr & SYS_BLE_FRAG_FIRST) {
    s_rx.len = 0;
    s_rx.expected = 0;
    s_rx.active = true;
  }
  if (!s_rx.active || count != s_rx.expected) {
    sys_ble_report(ERR_NEW(ERR_BLE_FRAG, .uuid = SYS_BLE_CHR_RUNIT_RX, .hdr = hdr, .expected = s_rx.expected));
    s_rx.active = false;  // the rest of this frame is dropped up to the next FIRST
    return;
  }
  size_t n = len - 1;
  if (s_rx.len + n > sizeof(s_rx.buf)) {
    sys_ble_report(ERR_NEW(ERR_BLE_TOO_LONG, .uuid = SYS_BLE_CHR_RUNIT_RX, .len = (uint16_t)(s_rx.len + n),
                           .max = sizeof(s_rx.buf)));
    s_rx.active = false;
    return;
  }
  memcpy(s_rx.buf + s_rx.len, data + 1, n);
  s_rx.len += n;
  s_rx.expected = (uint8_t)((s_rx.expected + 1) & SYS_BLE_FRAG_COUNT_MASK);

  if (hdr & SYS_BLE_FRAG_LAST) {
    s_rx.active = false;
    sys_ble_report(router_submit(TRANSPORT_BLE, s_rx.buf, (uint32_t)s_rx.len));
  }
}

static void on_conn(bool connected, int32_t reason) {
  (void)reason;
  if (!connected) {
    s_rx.active = false;
  }
}

/* ----------------------------------------------------------------------------
 * TX: router link
 * ------------------------------------------------------------------------- */

static err_h link_send(const uint8_t* frame, uint32_t len) {
  return sys_ble_char_send(SYS_BLE_CHR_RUNIT_TX, frame, len, LINK_SEND_WAIT_MS);
}

static uint32_t link_max_frame(void) {
  return (uint32_t)sys_ble_char_max_payload(SYS_BLE_CHR_RUNIT_TX);
}

static const router_link_t k_link = {
    .name = "ble",
    .send = link_send,
    .max_frame = link_max_frame,
};

/* ----------------------------------------------------------------------------
 * TX: connector provider - endpoint = characteristic UUID, so a stream can go
 * to any characteristic (LOGS, TX, or a user one a program streams on)
 * ------------------------------------------------------------------------- */

static err_h provider_send(uint32_t endpoint, const uint8_t* frame, uint32_t len) {
  ERR_CHECK(endpoint <= UINT16_MAX, ERR_BLE_NOT_FOUND, .uuid = 0);
  return sys_ble_char_send((uint16_t)endpoint, frame, len, PROVIDER_SEND_WAIT_MS);
}

static uint32_t provider_max_frame(uint32_t endpoint) {
  return endpoint <= UINT16_MAX ? (uint32_t)sys_ble_char_max_payload((uint16_t)endpoint) : 0;
}

static const sys_connector_provider_t k_provider = {
    .name = "ble",
    .send = provider_send,
    .max_frame = provider_max_frame,
};

/* ----------------------------------------------------------------------------
 * Init
 * ------------------------------------------------------------------------- */

err_h sys_ble_link_init(void) {
  const size_t frames = SYS_BLE_BUFF_SIZE_FOR(CONFIG_ROUTER_FRAME_MAX, TX_FRAMES);
  ERR_TRY(sys_ble_service_create(&(sys_ble_svc_cfg_t){.uuid = SYS_BLE_SVC_RUNIT, .is_primary = true}));
  ERR_TRY(sys_ble_char_create(SYS_BLE_SVC_RUNIT, &(sys_ble_char_cfg_t){
                                                     .uuid = SYS_BLE_CHR_RUNIT_RX,
                                                     .is_write = true,
                                                     .is_framed = true,
                                                     .desc = "runit RX",
                                                 }));
  ERR_TRY(sys_ble_char_create(SYS_BLE_SVC_RUNIT, &(sys_ble_char_cfg_t){
                                                     .uuid = SYS_BLE_CHR_RUNIT_TX,
                                                     .is_notify = true,
                                                     .is_framed = true,
                                                     .tx_buffer_size = frames,
                                                     .desc = "runit TX",
                                                 }));
  ERR_TRY(sys_ble_char_create(SYS_BLE_SVC_RUNIT, &(sys_ble_char_cfg_t){
                                                     .uuid = SYS_BLE_CHT_RUNIT_STATUS,
                                                     .is_notify = true,
                                                     .tx_buffer_size = STATUS_BUFFER,
                                                     .desc = "runit Status",
                                                 }));
  ERR_TRY(sys_ble_char_create(SYS_BLE_SVC_RUNIT, &(sys_ble_char_cfg_t){
                                                     .uuid = SYS_BLE_CHR_RUNIT_LOGS,
                                                     .is_notify = true,
                                                     .is_framed = true,
                                                     .tx_buffer_size = frames,
                                                     .desc = "runit LOGS",
                                                 }));
  // The command link lives here: a runtime change would rebuild the service and drop subscriptions.
  ERR_TRY(sys_ble_service_lock(SYS_BLE_SVC_RUNIT));

  ERR_TRY(sys_ble_char_set_rx_handler(SYS_BLE_CHR_RUNIT_RX, rx_packet, NULL));
  ERR_TRY(sys_ble_add_conn_listener(on_conn));
  ERR_TRY(router_link_register(TRANSPORT_BLE, &k_link));
  ERR_TRY(sys_connector_register_provider(TRANSPORT_BLE, &k_provider));
  return NULL;
}
