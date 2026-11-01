#include "sys_ble_link.h"

#include "sdkconfig.h"
#include "sys_ble.h"
#include "sys_ble_priv.h"
#include "sys_data_provider_driver.h"

#define TX_FRAMES     4u    // frames the TX / LOGS buffers hold
#define STATUS_BUFFER 512u  // size of the STATUS characteristic buffer

/* The two providers: the runIT pair (RX in, TX out; named by TX) and the LOGS characteristic (out only). Framed, up
 * while a client is connected. */
static sys_data_provider_t s_runit;
static sys_data_provider_t s_logs;

/* ----------------------------------------------------------------------------
 * RX: framed packets -> one frame -> the runIT provider's user (host task only)
 * ------------------------------------------------------------------------- */

static struct {
  uint8_t buf[CMD_FRAME_MAX];
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
    sys_data_provider_deliver(&s_runit, s_rx.buf, (uint32_t)s_rx.len);
  }
}

// A client is the providers' link: up while it is connected.
static void on_conn(bool connected, int32_t reason) {
  (void)reason;
  if (!connected) {
    s_rx.active = false;
  }
  sys_data_provider_set_max_frame(&s_runit, connected ? CMD_FRAME_MAX : 0);
  sys_data_provider_set_max_frame(&s_logs, connected ? CMD_FRAME_MAX : 0);
}

/* ----------------------------------------------------------------------------
 * TX: a frame to the provider's characteristic (TX or LOGS), split into packets by the BLE task
 * ------------------------------------------------------------------------- */

/* NOWAIT: only queued, a full buffer is ERR_PROV_BUSY at once; else it waits for room up to
 * CONFIG_SYS_BLE_ANSWER_WAIT_MS. Both characteristics' buffers keep their order: URGENT goes in turn. */
static err_h provider_send(sys_data_provider_t* self, const uint8_t* data, uint32_t len, uint8_t flags) {
  const uint32_t wait_ms = (flags & SYS_DATA_PROVIDER_SEND_NOWAIT) ? 0 : CONFIG_SYS_BLE_ANSWER_WAIT_MS;
  err_h err = sys_ble_char_send((uint16_t)self->id.channel, data, len, wait_ms);
  if (err != NULL && err->tag == ERR_BLE_TX_FULL) {
    err_free(err);
    return ERR_NEW(ERR_PROV_BUSY, .link = self->id.link, .channel = self->id.channel);
  }
  return err;
}

static err_h add_provider(sys_data_provider_t* p, uint16_t uuid) {
  *p = (sys_data_provider_t){
      .send = provider_send,
      .max_frame = 0,  // up when a client connects
      .id = {.link = SYS_DATA_PROVIDER_LINK_BLE, .channel = uuid},
      .state = SYS_DATA_PROVIDER_STATE_FRAMED,
  };
  return sys_data_provider_add(p);
}

/* ----------------------------------------------------------------------------
 * Init
 * ------------------------------------------------------------------------- */

err_h sys_ble_link_init(void) {
  const size_t frames = SYS_BLE_BUFF_SIZE_FOR(CMD_FRAME_MAX, TX_FRAMES);
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
  // The session's provider lives here: a runtime change would rebuild the service and drop subscriptions.
  ERR_TRY(sys_ble_service_lock(SYS_BLE_SVC_RUNIT));

  ERR_TRY(sys_ble_char_set_rx_handler(SYS_BLE_CHR_RUNIT_RX, rx_packet, NULL));
  ERR_TRY(sys_ble_add_conn_listener(on_conn));
  ERR_TRY(add_provider(&s_runit, SYS_BLE_CHR_RUNIT_TX));
  ERR_TRY(add_provider(&s_logs, SYS_BLE_CHR_RUNIT_LOGS));
  return NULL;
}
