#include "sys_ble_central.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/timers.h"
#include "host/ble_hs.h"
#include "sys_ble_priv.h"
#include "sys_data_provider_driver.h"

#define TAG __FILE_NAME__

#define RETRY_MS           1000u  // a link that is down tries to connect this often
#define CONNECT_MS         5000u  // one try: how long the board is looked for
#define TX_FRAMES          4u     // frames a link's queue holds
/* A link's interval: 30 to 50 ms (1.25 ms units). The radio also keeps the app's link (7.5 ms) and the advertising: a second
   link at 7.5 ms is refused by the controller (0x0D, limited resources). The other board asks for 7.5 ms as every
   peripheral does: the link answers with this. */
#define CONN_ITVL_MIN_UNITS 24u
#define CONN_ITVL_MAX_UNITS 40u
#define CONN_TIMEOUT_UNITS  400u  // 4 s supervision timeout (10 ms units)
#define SCAN_ITVL_UNITS    0x50u  // 50 ms (0.625 ms units)
#define SCAN_WINDOW_UNITS  0x30u  // 30 ms of it listening: the board's own advertising and links keep their time

// What a link is doing; ERR_BLE_CENTRAL's `step` is the one that failed.
typedef enum {
  LINK_DOWN = 0,    // not connected: the timer tries again
  LINK_CONNECTING,  // ble_gap_connect running
  LINK_SERVICE,     // connected: looking for the runIT service
  LINK_CHARS,       // looking for its TX and RX
  LINK_SUBSCRIBE,   // writing TX's CCCD
  LINK_UP,          // frames go both ways
} link_state_e;

/* A link to another board's runIT pair. Allocated at its first CONNECT and kept for the next one (a GAP or GATT
 * callback may still name it after its provider is removed); `used` while its provider exists. */
typedef struct {
  sys_data_provider_t p;
  ble_addr_t addr;
  RingbufHandle_t tx;  // whole frames to send, split by the BLE task
  sys_ble_frag_rx_t rx;
  uint16_t conn;       // BLE_HS_CONN_HANDLE_NONE when not connected
  uint16_t mtu;
  uint16_t svc_start;
  uint16_t svc_end;
  uint16_t tx_handle;  // the other board's runIT TX (0xFFE1) value: its notifications come from it
  uint16_t rx_handle;  // its runIT RX (0xFFE2) value: this board writes to it
  uint8_t state;       // link_state_e
  bool used;
} link_t;

static link_t* s_links[CONFIG_SYS_BLE_CENTRAL_LINKS];
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;  // a link's state, conn, handles; the found boards

static sys_ble_found_t s_found[CONFIG_SYS_BLE_CENTRAL_FOUND_MAX];
static uint8_t s_found_count;
static bool s_scanning;

static uint8_t s_in[BLE_ATT_MTU_MAX];   // host task only: one notification
static uint8_t s_out[BLE_ATT_MTU_MAX];  // BLE task only: one packet

static void retry_tick(TimerHandle_t t);
U_TIMER_DEFINE(sys_ble_central_retry, MSEC(RETRY_MS), pdTRUE, retry_tick);

static int link_event(struct ble_gap_event* event, void* arg);

// The link of slot `i` while its provider exists; NULL otherwise. Callbacks name a link by its slot.
static link_t* link_at(uintptr_t i) {
  return i < ARRAY_LEN(s_links) && s_links[i] != NULL && s_links[i]->used ? s_links[i] : NULL;
}

static void fail(link_t* l, link_state_e step, int rc) {
  sys_ble_report(ERR_NEW(ERR_BLE_CENTRAL, .provider = l->p.id, .step = (uint8_t)step, .rc = rc));
  if (l->conn != BLE_HS_CONN_HANDLE_NONE) {
    ble_gap_terminate(l->conn, BLE_ERR_REM_USER_CONN_TERM);  // its DISCONNECT puts it down
  } else {
    l->state = LINK_DOWN;
  }
}

/* ----------------------------------------------------------------------------
 * Connecting (timer task) and discovery (host task)
 * ------------------------------------------------------------------------- */

static void try_connect(uintptr_t i) {
  uint8_t own;
  link_t* l = link_at(i);
  if (l == NULL || l->state != LINK_DOWN || !sys_ble_stack_own_addr(&own)) {
    return;
  }
  const struct ble_gap_conn_params params = {
      .scan_itvl = SCAN_ITVL_UNITS,
      .scan_window = SCAN_WINDOW_UNITS,
      .itvl_min = CONN_ITVL_MIN_UNITS,
      .itvl_max = CONN_ITVL_MAX_UNITS,
      .latency = 0,
      .supervision_timeout = CONN_TIMEOUT_UNITS,
  };
  l->state = LINK_CONNECTING;
  const int rc = ble_gap_connect(own, &l->addr, CONNECT_MS, &params, link_event, (void*)i);
  if (rc != 0) {
    l->state = LINK_DOWN;  // another connect or a scan runs (EALREADY, EBUSY): the next tick
  }
}

// One link at a time: NimBLE makes one connection at once.
static void retry_tick(TimerHandle_t t) {
  (void)t;
  for (uintptr_t i = 0; i < ARRAY_LEN(s_links); i++) {
    if (link_at(i) != NULL && s_links[i]->state == LINK_DOWN) {
      try_connect(i);
      return;
    }
  }
}

static int on_subscribed(uint16_t conn, const struct ble_gatt_error* error, struct ble_gatt_attr* attr, void* arg) {
  (void)conn, (void)attr;
  link_t* l = link_at((uintptr_t)arg);
  if (l == NULL || l->state != LINK_SUBSCRIBE) {
    return 0;
  }
  if (error->status != 0) {
    fail(l, LINK_SUBSCRIBE, error->status);
    return 0;
  }
  portENTER_CRITICAL(&s_lock);
  l->state = LINK_UP;
  portEXIT_CRITICAL(&s_lock);
  ESP_LOGI(TAG, "link %u up (handle %u)", l->p.id, l->conn);
  sys_data_provider_set_up(&l->p, true);
  return 0;
}

static int on_chr(uint16_t conn, const struct ble_gatt_error* error, const struct ble_gatt_chr* chr, void* arg) {
  link_t* l = link_at((uintptr_t)arg);
  if (l == NULL || l->state != LINK_CHARS) {
    return 0;
  }
  if (error->status == 0) {
    const uint16_t uuid = ble_uuid_u16(&chr->uuid.u);
    if (uuid == SYS_BLE_CHR_RUNIT_TX) {
      l->tx_handle = chr->val_handle;
    } else if (uuid == SYS_BLE_CHR_RUNIT_RX) {
      l->rx_handle = chr->val_handle;
    }
    return 0;
  }
  if (error->status != BLE_HS_EDONE || l->tx_handle == 0 || l->rx_handle == 0) {
    fail(l, LINK_CHARS, error->status != BLE_HS_EDONE ? error->status : BLE_HS_ENOENT);
    return 0;
  }
  /* NimBLE puts a characteristic's CCCD right after its value (ble_gatts.c), and the other board runs this firmware:
     notifications on. */
  static const uint8_t notify_on[2] = {1, 0};
  l->state = LINK_SUBSCRIBE;
  const int rc = ble_gattc_write_flat(conn, l->tx_handle + 1, notify_on, sizeof(notify_on), on_subscribed, arg);
  if (rc != 0) {
    fail(l, LINK_SUBSCRIBE, rc);
  }
  return 0;
}

static int on_svc(uint16_t conn, const struct ble_gatt_error* error, const struct ble_gatt_svc* svc, void* arg) {
  link_t* l = link_at((uintptr_t)arg);
  if (l == NULL || l->state != LINK_SERVICE) {
    return 0;
  }
  if (error->status == 0) {
    l->svc_start = svc->start_handle;
    l->svc_end = svc->end_handle;
    return 0;
  }
  if (error->status != BLE_HS_EDONE || l->svc_start == 0) {
    fail(l, LINK_SERVICE, error->status != BLE_HS_EDONE ? error->status : BLE_HS_ENOENT);
    return 0;
  }
  l->state = LINK_CHARS;
  const int rc = ble_gattc_disc_all_chrs(conn, l->svc_start, l->svc_end, on_chr, arg);
  if (rc != 0) {
    fail(l, LINK_CHARS, rc);
  }
  return 0;
}

static void link_down(link_t* l) {
  portENTER_CRITICAL(&s_lock);
  const bool was_up = l->state == LINK_UP;
  l->state = LINK_DOWN;
  l->conn = BLE_HS_CONN_HANDLE_NONE;
  portEXIT_CRITICAL(&s_lock);
  sys_ble_frag_rx_reset(&l->rx);
  size_t size;
  void* item;
  while ((item = xRingbufferReceive(l->tx, &size, NO_WAIT)) != NULL) {
    vRingbufferReturnItem(l->tx, item);
  }
  if (was_up) {
    sys_data_provider_set_up(&l->p, false);
  }
}

static int link_event(struct ble_gap_event* event, void* arg) {
  link_t* l = link_at((uintptr_t)arg);
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      if (l == NULL) {
        if (event->connect.status == 0) {
          ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);  // removed meanwhile
        }
        return 0;
      }
      if (event->connect.status != 0) {
        l->state = LINK_DOWN;  // not found in CONNECT_MS: the timer tries again
        return 0;
      }
      portENTER_CRITICAL(&s_lock);
      l->conn = event->connect.conn_handle;
      l->mtu = ble_att_mtu(l->conn) != 0 ? ble_att_mtu(l->conn) : BLE_ATT_MTU_DFLT;
      l->svc_start = l->svc_end = l->tx_handle = l->rx_handle = 0;
      l->state = LINK_SERVICE;
      portEXIT_CRITICAL(&s_lock);
      ESP_LOGI(TAG, "link %u connected (handle %u)", l->p.id, l->conn);
      // The other board starts the MTU exchange itself, as it does with every client.
      static const ble_uuid16_t runit = BLE_UUID16_INIT(SYS_BLE_SVC_RUNIT);
      const int rc = ble_gattc_disc_svc_by_uuid(l->conn, &runit.u, on_svc, arg);
      if (rc != 0) {
        fail(l, LINK_SERVICE, rc);
      }
      return 0;

    case BLE_GAP_EVENT_DISCONNECT:
      if (l != NULL) {
        ESP_LOGI(TAG, "link %u lost (reason 0x%x)", l->p.id, event->disconnect.reason);
        link_down(l);
      }
      return 0;

    // The other board asks for its own interval (7.5 ms): this board keeps the link's.
    case BLE_GAP_EVENT_CONN_UPDATE_REQ:
    case BLE_GAP_EVENT_L2CAP_UPDATE_REQ:
      *event->conn_update_req.self_params = (struct ble_gap_upd_params){
          .itvl_min = CONN_ITVL_MIN_UNITS,
          .itvl_max = CONN_ITVL_MAX_UNITS,
          .latency = 0,
          .supervision_timeout = CONN_TIMEOUT_UNITS,
      };
      return 0;

    case BLE_GAP_EVENT_MTU:
      if (l != NULL) {
        portENTER_CRITICAL(&s_lock);
        l->mtu = event->mtu.value;
        portEXIT_CRITICAL(&s_lock);
      }
      return 0;

    case BLE_GAP_EVENT_NOTIFY_RX: {
      if (l == NULL || l->state != LINK_UP || event->notify_rx.attr_handle != l->tx_handle) {
        return 0;
      }
      const uint16_t len = OS_MBUF_PKTLEN(event->notify_rx.om);
      if (len == 0 || len > sizeof(s_in)) {
        return 0;
      }
      os_mbuf_copydata(event->notify_rx.om, 0, len, s_in);
      const size_t frame = sys_ble_frag_rx(&l->rx, SYS_BLE_CHR_RUNIT_TX, s_in, len);
      if (frame > 0) {
        sys_data_provider_deliver(&l->p, l->rx.buf, (uint32_t)frame);
      }
      return 0;
    }

    default:
      return 0;
  }
}

/* ----------------------------------------------------------------------------
 * The provider: frames queued by any task, written by the BLE task
 * ------------------------------------------------------------------------- */

static err_h link_send(sys_data_provider_t* self, const uint8_t* data, uint32_t len, uint8_t flags) {
  link_t* l = self->driver;
  ERR_CHECK(len <= CMD_FRAME_MAX, ERR_PROV_TOO_LONG, .provider = self->id, .len = (uint16_t)len, .max = CMD_FRAME_MAX);
  portENTER_CRITICAL(&s_lock);
  const bool up = l->state == LINK_UP;
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(up, ERR_PROV_DOWN, .provider = self->id);
  const uint32_t wait_ms = (flags & SYS_DATA_PROVIDER_SEND_NOWAIT) ? 0 : CONFIG_SYS_BLE_ANSWER_WAIT_MS;
  ERR_CHECK(xRingbufferSend(l->tx, data, len, MSEC(wait_ms)) == pdTRUE, ERR_PROV_BUSY, .provider = self->id);
  sys_ble_wake();
  return NULL;
}

static void link_destroy(sys_data_provider_t* self) {
  link_t* l = self->driver;
  portENTER_CRITICAL(&s_lock);
  l->used = false;
  const uint16_t conn = l->conn;
  portEXIT_CRITICAL(&s_lock);
  if (conn != BLE_HS_CONN_HANDLE_NONE) {
    ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
  } else if (l->state == LINK_CONNECTING) {
    ble_gap_conn_cancel();
  }
  bool any = false;
  for (size_t i = 0; i < ARRAY_LEN(s_links); i++) {
    any = any || link_at(i) != NULL;
  }
  if (!any) {
    xTimerStop(sys_ble_central_retry, 0);
  }
}

err_h sys_ble_central_connect(uint8_t provider, uint8_t addr_type, const uint8_t addr[6]) {
  ERR_CHECK(addr != NULL && addr_type <= BLE_ADDR_RANDOM, ERR_BLE_BAD_ARG);
  for (size_t k = 0; k < ARRAY_LEN(s_links); k++) {
    const link_t* l = link_at(k);
    if (l != NULL && l->p.id == provider && l->addr.type == addr_type && memcmp(l->addr.val, addr, 6) == 0) {
      return NULL;  // this link already: a stored project's record run again
    }
  }
  size_t i = 0;
  while (i < ARRAY_LEN(s_links) && s_links[i] != NULL && s_links[i]->used) {
    i++;
  }
  ERR_CHECK(i < ARRAY_LEN(s_links), ERR_BLE_NO_MEM, .uuid = 0, .size = sizeof(s_links));
  if (s_links[i] == NULL) {
    link_t* l = calloc(1, sizeof(*l));
    ERR_CHECK(l != NULL, ERR_BLE_NO_MEM, .uuid = 0, .size = sizeof(*l));
    l->tx = xRingbufferCreate(SYS_BLE_BUFF_SIZE_FOR(CMD_FRAME_MAX, TX_FRAMES), RINGBUF_TYPE_NOSPLIT);
    if (l->tx == NULL) {
      free(l);
      return ERR_NEW(ERR_BLE_NO_MEM, .uuid = 0, .size = SYS_BLE_BUFF_SIZE_FOR(CMD_FRAME_MAX, TX_FRAMES));
    }
    s_links[i] = l;
  }
  link_t* l = s_links[i];
  l->p = (sys_data_provider_t){
      .send = link_send,
      .destroy = link_destroy,
      .driver = l,
      .id = provider,
      .link = SYS_DATA_PROVIDER_LINK_BLE,
      .state = SYS_DATA_PROVIDER_STATE_FRAMED,  // not WIRED: a radio; up while connected and subscribed
  };
  l->addr = (ble_addr_t){.type = addr_type};
  memcpy(l->addr.val, addr, sizeof(l->addr.val));
  l->conn = BLE_HS_CONN_HANDLE_NONE;
  l->state = LINK_DOWN;
  sys_ble_frag_rx_reset(&l->rx);
  ERR_TRY(sys_data_provider_add(&l->p));
  portENTER_CRITICAL(&s_lock);
  l->used = true;
  portEXIT_CRITICAL(&s_lock);
  xTimerStart(sys_ble_central_retry, 0);  // before the stack runs too: the first tick after it does connects
  return NULL;
}

/* ----------------------------------------------------------------------------
 * The BLE task: each link's queued frames, as packets written without response
 * ------------------------------------------------------------------------- */

typedef struct {
  link_t* l;
  uint16_t conn;
  uint16_t handle;
} write_to_t;

// One packet; retries while NimBLE has no buffer and the link is the same. False when it can't go.
static bool write_packet(void* ctx, const uint8_t* pkt, size_t len) {
  const write_to_t* to = ctx;
  int rc;
  while ((rc = ble_gattc_write_no_rsp_flat(to->conn, to->handle, pkt, (uint16_t)len)) == BLE_HS_ENOMEM) {
    vTaskDelay(MSEC(10));
    portENTER_CRITICAL(&s_lock);
    const bool same = to->l->state == LINK_UP && to->l->conn == to->conn;
    portEXIT_CRITICAL(&s_lock);
    if (!same) {
      return false;
    }
  }
  if (rc != 0 && rc != BLE_HS_ENOTCONN) {
    sys_ble_report(ERR_NEW(ERR_BLE_STACK, .rc = rc));
  }
  return rc == 0;
}

void sys_ble_central_step(void) {
  for (size_t i = 0; i < ARRAY_LEN(s_links); i++) {
    link_t* l = s_links[i];
    while (l != NULL) {
      portENTER_CRITICAL(&s_lock);
      write_to_t to = {.l = l, .conn = l->conn, .handle = l->rx_handle};
      const uint16_t mtu = l->mtu;
      const bool up = l->used && l->state == LINK_UP;
      portEXIT_CRITICAL(&s_lock);
      size_t size = 0;
      uint8_t* item = up ? xRingbufferReceive(l->tx, &size, NO_WAIT) : NULL;
      if (item == NULL) {
        break;
      }
      sys_ble_frag_send(item, size, (size_t)(mtu - SYS_BLE_ATT_HDR_LEN), s_out, write_packet, &to);
      vRingbufferReturnItem(l->tx, item);
    }
  }
}

/* ----------------------------------------------------------------------------
 * Scan (host task for its events)
 * ------------------------------------------------------------------------- */

static bool runit_board(const struct ble_hs_adv_fields* f) {
  for (uint8_t i = 0; i < f->num_uuids16; i++) {
    if (ble_uuid_u16(&f->uuids16[i].u) == SYS_BLE_SVC_RUNIT) {
      return true;
    }
  }
  return false;
}

static void found(const struct ble_gap_disc_desc* d) {
  struct ble_hs_adv_fields f;
  if (ble_hs_adv_parse_fields(&f, d->data, d->length_data) != 0 || !runit_board(&f)) {
    return;
  }
  portENTER_CRITICAL(&s_lock);
  uint8_t i = 0;
  while (i < s_found_count && (s_found[i].addr_type != d->addr.type ||
                               memcmp(s_found[i].addr, d->addr.val, sizeof(s_found[i].addr)) != 0)) {
    i++;
  }
  if (i < ARRAY_LEN(s_found)) {
    sys_ble_found_t* e = &s_found[i];
    if (i == s_found_count) {
      s_found_count++;
      *e = (sys_ble_found_t){.addr_type = d->addr.type};
      memcpy(e->addr, d->addr.val, sizeof(e->addr));
    }
    e->rssi = d->rssi;
    if (f.name_len > 0) {
      const size_t n = f.name_len < sizeof(e->name) - 1 ? f.name_len : sizeof(e->name) - 1;
      memset(e->name, 0, sizeof(e->name));
      memcpy(e->name, f.name, n);
    }
  }
  portEXIT_CRITICAL(&s_lock);
}

static int scan_event(struct ble_gap_event* event, void* arg) {
  (void)arg;
  if (event->type == BLE_GAP_EVENT_DISC) {
    found(&event->disc);
  } else if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
    portENTER_CRITICAL(&s_lock);
    s_scanning = false;
    portEXIT_CRITICAL(&s_lock);
  }
  return 0;
}

err_h sys_ble_central_scan(uint16_t duration_ms) {
  uint8_t own;
  ERR_CHECK(sys_ble_stack_own_addr(&own), ERR_BLE_NOT_STARTED);
  if (ble_gap_disc_active()) {
    ble_gap_disc_cancel();  // a new scan starts over
  }
  portENTER_CRITICAL(&s_lock);
  s_found_count = 0;
  s_scanning = true;
  portEXIT_CRITICAL(&s_lock);
  const struct ble_gap_disc_params params = {
      .itvl = SCAN_ITVL_UNITS,
      .window = SCAN_WINDOW_UNITS,
      .filter_duplicates = 0,  // the RSSI follows the board
  };
  const int rc = ble_gap_disc(own, duration_ms, &params, scan_event, NULL);
  if (rc != 0) {
    portENTER_CRITICAL(&s_lock);
    s_scanning = false;
    portEXIT_CRITICAL(&s_lock);
  }
  ERR_CHECK(rc == 0, ERR_BLE_SCAN, .rc = rc);
  return NULL;
}

uint8_t sys_ble_central_found(sys_ble_found_t* out, uint8_t max, bool* scanning) {
  portENTER_CRITICAL(&s_lock);
  const uint8_t n = s_found_count < max ? s_found_count : max;
  memcpy(out, s_found, n * sizeof(*out));
  *scanning = s_scanning;
  portEXIT_CRITICAL(&s_lock);
  return n;
}
