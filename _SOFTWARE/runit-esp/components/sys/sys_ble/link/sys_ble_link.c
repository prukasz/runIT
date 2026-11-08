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

// The providers of characteristics made at run time (sys_ble_link_char_add), in a static table.
static struct {
  sys_data_provider_t p;
  uint16_t svc;
  bool used;
} s_user[CONFIG_SYS_BLE_USER_PROVIDERS];

/* ----------------------------------------------------------------------------
 * RX: framed packets -> one frame -> the runIT provider's user (host task only)
 * ------------------------------------------------------------------------- */

static sys_ble_frag_rx_t s_rx;

static void rx_packet(void* ctx, const uint8_t* data, size_t len) {
  (void)ctx;
  const size_t frame = sys_ble_frag_rx(&s_rx, SYS_BLE_CHR_RUNIT_RX, data, len);
  if (frame > 0) {
    sys_data_provider_deliver(&s_runit, s_rx.buf, (uint32_t)frame);
  }
}

// Whether a client is connected: the providers' link.
static bool client_connected(void) {
  sys_ble_status_t status;
  err_h err = sys_ble_get_status(&status);
  if (err != NULL) {
    err_free(err);
    return false;
  }
  return status.is_connected;
}

// A client is the providers' link: up while it is connected.
static void on_conn(bool connected, int32_t reason) {
  (void)reason;
  if (!connected) {
    sys_ble_frag_rx_reset(&s_rx);
  }
  sys_data_provider_set_up(&s_runit, connected);
  sys_data_provider_set_up(&s_logs, connected);
  for (size_t i = 0; i < ARRAY_LEN(s_user); i++) {
    if (s_user[i].used) {
      sys_data_provider_set_up(&s_user[i].p, connected);
    }
  }
}

/* ----------------------------------------------------------------------------
 * TX: a frame to the provider's characteristic (TX or LOGS), split into packets by the BLE task
 * ------------------------------------------------------------------------- */

// A provider's characteristic: the driver's own field holds its UUID.
static uint16_t char_of(const sys_data_provider_t* p) {
  return (uint16_t)(uintptr_t)p->driver;
}

/* NOWAIT: only queued, a full buffer is ERR_PROV_BUSY at once; else it waits for room up to
 * CONFIG_SYS_BLE_ANSWER_WAIT_MS. Both characteristics' buffers keep their order: URGENT goes in turn. */
static err_h provider_send(sys_data_provider_t* self, const uint8_t* data, uint32_t len, uint8_t flags) {
  const uint32_t wait_ms = (flags & SYS_DATA_PROVIDER_SEND_NOWAIT) ? 0 : CONFIG_SYS_BLE_ANSWER_WAIT_MS;
  err_h err = sys_ble_char_send(char_of(self), data, len, wait_ms);
  if (err != NULL && err->tag == ERR_BLE_TX_FULL) {
    err_free(err);
    return ERR_NEW(ERR_PROV_BUSY, .provider = self->id);
  }
  return err;
}

/* A run-time characteristic's send: its peer knows no fragments, so a message longer than one notification goes as
 * several, in order. A full buffer part way through loses the rest (ERR_PROV_BUSY). */
static err_h user_send(sys_data_provider_t* self, const uint8_t* data, uint32_t len, uint8_t flags) {
  const size_t max = sys_ble_char_max_payload(char_of(self));
  if (max == 0) {
    return provider_send(self, data, len, flags);  // the characteristic's own error
  }
  for (uint32_t done = 0; done < len;) {
    const uint32_t piece = len - done < max ? len - done : (uint32_t)max;
    ERR_TRY(provider_send(self, data + done, piece, flags));
    done += piece;
  }
  return NULL;
}

static err_h add_provider(sys_data_provider_t* p, uint8_t id, uint16_t uuid) {
  *p = (sys_data_provider_t){
      .send = provider_send,
      .driver = (void*)(uintptr_t)uuid,
      .id = id,
      .link = SYS_DATA_PROVIDER_LINK_BLE,
      .state = SYS_DATA_PROVIDER_STATE_SYSTEM | SYS_DATA_PROVIDER_STATE_FRAMED,  // up when a client connects
  };
  return sys_data_provider_add(p);
}

/* ----------------------------------------------------------------------------
 * Providers of characteristics made at run time
 * ------------------------------------------------------------------------- */

// A write on the characteristic: one message to the provider's user (the BLE host task).
static void user_rx(void* ctx, const uint8_t* data, size_t len) {
  sys_data_provider_deliver(ctx, data, (uint32_t)len);
}

static void user_destroy(sys_data_provider_t* self) {
  for (size_t i = 0; i < ARRAY_LEN(s_user); i++) {
    if (&s_user[i].p == self) {
      s_user[i].used = false;
    }
  }
}

err_h sys_ble_link_char_add(uint16_t svc_uuid, const sys_ble_char_cfg_t* cfg, uint8_t provider) {
  const bool rx = cfg->is_write && cfg->rx_buffer_size == 0;
  const bool tx = (cfg->is_notify || cfg->is_indicate) && cfg->tx_buffer_size > 0;
  if (cfg->is_framed || (!rx && !tx)) {
    return NULL;
  }
  size_t i = 0;
  while (i < ARRAY_LEN(s_user) && s_user[i].used) {
    i++;
  }
  ERR_CHECK(i < ARRAY_LEN(s_user), ERR_BLE_NO_MEM, .uuid = cfg->uuid, .size = sizeof(s_user));
  s_user[i].p = (sys_data_provider_t){
      .send = user_send,
      .destroy = user_destroy,
      .driver = (void*)(uintptr_t)cfg->uuid,
      .id = provider,
      .link = SYS_DATA_PROVIDER_LINK_BLE,
      .state = client_connected() ? SYS_DATA_PROVIDER_STATE_UP : 0u,
  };
  s_user[i].svc = svc_uuid;
  s_user[i].used = true;
  err_h err = sys_data_provider_add(&s_user[i].p);
  if (err != NULL) {
    s_user[i].used = false;  // not added: the id may be another provider's, which stays
    return err;
  }
  if (rx) {
    err = sys_ble_char_set_rx_handler(cfg->uuid, user_rx, &s_user[i].p);
    if (err != NULL) {
      err_report(sys_data_provider_remove(s_user[i].p.id));  // its destroy frees the slot
    }
  }
  return err;
}

err_h sys_ble_link_char_remove(uint16_t char_uuid) {
  for (size_t i = 0; i < ARRAY_LEN(s_user); i++) {
    if (s_user[i].used && char_of(&s_user[i].p) == char_uuid) {
      return sys_data_provider_remove(s_user[i].p.id);
    }
  }
  return NULL;
}

bool sys_ble_link_char_provider(uint16_t char_uuid, uint8_t* provider) {
  const sys_data_provider_t* found = NULL;
  if (char_of(&s_runit) == char_uuid) {
    found = &s_runit;
  } else if (char_of(&s_logs) == char_uuid) {
    found = &s_logs;
  }
  for (size_t i = 0; found == NULL && i < ARRAY_LEN(s_user); i++) {
    if (s_user[i].used && char_of(&s_user[i].p) == char_uuid) {
      found = &s_user[i].p;
    }
  }
  if (found != NULL) {
    *provider = found->id;
  }
  return found != NULL;
}

err_h sys_ble_link_service_remove(uint16_t svc_uuid) {
  for (size_t i = 0; i < ARRAY_LEN(s_user); i++) {
    if (s_user[i].used && s_user[i].svc == svc_uuid) {
      ERR_TRY(sys_data_provider_remove(s_user[i].p.id));
    }
  }
  return NULL;
}

/* ----------------------------------------------------------------------------
 * Init
 * ------------------------------------------------------------------------- */

err_h sys_ble_link_init(uint8_t runit_provider, uint8_t logs_provider) {
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
  ERR_TRY(add_provider(&s_runit, runit_provider, SYS_BLE_CHR_RUNIT_TX));
  ERR_TRY(add_provider(&s_logs, logs_provider, SYS_BLE_CHR_RUNIT_LOGS));
  return NULL;
}
