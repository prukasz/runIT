

#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "sys_ble_priv.h"

#define TAG __FILE_NAME__

#define ADV_ITVL_MIN_MS    20u  // fastest advertising the spec allows: quick (re)connects
#define ADV_ITVL_MAX_MS    30u
#define CONN_ITVL_UNITS    6u     // 7.5 ms (1.25 ms units): the spec minimum, lowest link latency
#define CONN_SLAVE_LATENCY 0u  // answer every connection event
#define CONN_TIMEOUT_UNITS 200u  // 2 s supervision timeout (10 ms units)
#define UUID_USER_DESC     0x2901u

void ble_store_config_init(void);

U_TASK_DEFINE(sys_ble_task_h, CONFIG_SYS_BLE_TASK_STACK);

static uint8_t s_own_addr_type;
static uint8_t s_tx_scratch[CMD_FRAME_MAX];  // BLE task only

static int gap_event(struct ble_gap_event* event, void* arg);

/* ----------------------------------------------------------------------------
 * Advertising
 * ------------------------------------------------------------------------- */

static err_h adv_start(void) {
  const char* name = ble_svc_gap_device_name();
  /* The runIT service in the advertisement: a client filters by it, and reconnects to a known board on seeing it. */
  static const ble_uuid16_t runit_svc = BLE_UUID16_INIT(SYS_BLE_SVC_RUNIT);
  struct ble_hs_adv_fields fields = {
      .uuids16 = &runit_svc,
      .num_uuids16 = 1,
      .uuids16_is_complete = 1,
      .flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,
      .name = (uint8_t*)name,
      .name_len = (uint8_t)strlen(name),
      .name_is_complete = 1,
      .tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO,
      .tx_pwr_lvl_is_present = 1,
      .appearance = CONFIG_SYS_BLE_GAP_APPEARANCE,
      .appearance_is_present = 1,
  };
  int rc = ble_gap_adv_set_fields(&fields);
  ERR_CHECK(rc == 0, ERR_BLE_ADV, .rc = rc);

  struct ble_gap_adv_params params = {
      .conn_mode = BLE_GAP_CONN_MODE_UND,
      .disc_mode = BLE_GAP_DISC_MODE_GEN,
      .itvl_min = BLE_GAP_ADV_ITVL_MS(ADV_ITVL_MIN_MS),
      .itvl_max = BLE_GAP_ADV_ITVL_MS(ADV_ITVL_MAX_MS),
  };
  rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
  ERR_CHECK(rc == 0 || rc == BLE_HS_EALREADY, ERR_BLE_ADV, .rc = rc);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * GAP events (host task)
 * ------------------------------------------------------------------------- */

static int gap_event(struct ble_gap_event* event, void* arg) {
  (void)arg;
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status == 0) {
        /* A client that starts the MTU exchange itself (Windows) can finish it
         * before this event: take the link's current MTU, not the default. */
        uint16_t mtu = ble_att_mtu(event->connect.conn_handle);
        sys_ble_on_connect(event->connect.conn_handle, mtu != 0 ? mtu : BLE_ATT_MTU_DFLT);
        // Ask for the largest MTU now; EALREADY = the client already started it.
        int rc = ble_gattc_exchange_mtu(event->connect.conn_handle, NULL, NULL);
        if (rc != 0 && rc != BLE_HS_EALREADY) {
          ESP_LOGW(TAG, "MTU exchange request failed: %d", rc);
        }
        struct ble_gap_upd_params upd = {
            .itvl_min = CONN_ITVL_UNITS,
            .itvl_max = CONN_ITVL_UNITS,
            .latency = CONN_SLAVE_LATENCY,
            .supervision_timeout = CONN_TIMEOUT_UNITS,
        };
        rc = ble_gap_update_params(event->connect.conn_handle, &upd);  // the central may still refuse or round up
        if (rc != 0) {
          ESP_LOGW(TAG, "connection parameter update failed: %d", rc);
        }
        ESP_LOGI(TAG, "connected (handle %d, MTU %d)", event->connect.conn_handle, mtu);
      } else {
        ESP_LOGW(TAG, "connection failed: %d", event->connect.status);
        sys_ble_report(adv_start());
      }
      return 0;

    case BLE_GAP_EVENT_DISCONNECT:
      ESP_LOGI(TAG, "disconnected (reason 0x%x)", event->disconnect.reason);
      sys_ble_on_disconnect(event->disconnect.reason);
      sys_ble_report(adv_start());  // a failed restart leaves the board invisible: logged
      return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
      sys_ble_report(adv_start());
      return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
      sys_ble_on_subscribe(event->subscribe.attr_handle, event->subscribe.cur_notify || event->subscribe.cur_indicate);
      return 0;

    case BLE_GAP_EVENT_CONN_UPDATE: {
      struct ble_gap_conn_desc desc;
      if (event->conn_update.status == 0 && ble_gap_conn_find(event->conn_update.conn_handle, &desc) == 0) {
        ESP_LOGI(TAG, "link params: interval %u.%02u ms, latency %u, timeout %u ms", desc.conn_itvl * 5 / 4,
                 desc.conn_itvl * 125 % 100, desc.conn_latency, desc.supervision_timeout * 10);
      }
      return 0;
    }

    case BLE_GAP_EVENT_MTU:
      sys_ble_on_mtu(event->mtu.value);
      ESP_LOGI(TAG, "MTU %d", event->mtu.value);
      return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
      struct ble_gap_conn_desc desc;
      if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
        ble_store_util_delete_peer(&desc.peer_id_addr);
      }
      return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    default:
      return 0;
  }
}

/* ----------------------------------------------------------------------------
 * GATT access (host task)
 * ------------------------------------------------------------------------- */

static int chr_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg) {
  (void)conn_handle, (void)attr_handle;
  sys_ble_char_node_t* c = arg;
  if (c == NULL || ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  uint8_t data[BLE_ATT_ATTR_MAX_LEN];
  uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
  if (len == 0) {
    return 0;
  }
  if (len > sizeof(data)) {
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  }
  os_mbuf_copydata(ctxt->om, 0, len, data);
  return sys_ble_on_write(c, data, len);
}

static int dsc_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg) {
  (void)conn_handle, (void)attr_handle;
  const char* desc = arg;
  if (desc == NULL) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  return os_mbuf_append(ctxt->om, desc, strlen(desc)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

/* ----------------------------------------------------------------------------
 * Table compile (NimBLE keeps pointers into these definitions)
 * ------------------------------------------------------------------------- */

static ble_uuid_t* new_uuid16(uint16_t value) {
  ble_uuid16_t* u = malloc(sizeof(*u));
  if (u != NULL) {
    *u = (ble_uuid16_t)BLE_UUID16_INIT(value);
  }
  return (ble_uuid_t*)u;
}

static err_h compile_chars(struct ble_gatt_chr_def** out, sys_ble_svc_node_t* s) {
  size_t count = 0;
  sys_ble_char_node_t* c;
  LL_FOREACH(s->chars, c) {
    count++;
  }
  struct ble_gatt_chr_def* defs = calloc(count + 1, sizeof(*defs));
  ERR_CHECK(defs != NULL, ERR_BLE_NO_MEM, .uuid = s->cfg.uuid, .size = (uint32_t)((count + 1) * sizeof(*defs)));
  *out = defs;

  size_t i = 0;
  LL_FOREACH(s->chars, c) {
    struct ble_gatt_chr_def* d = &defs[i++];
    d->uuid = new_uuid16(c->cfg.uuid);
    ERR_CHECK(d->uuid != NULL, ERR_BLE_NO_MEM, .uuid = c->cfg.uuid, .size = sizeof(ble_uuid16_t));
    d->access_cb = chr_access;
    d->arg = c;
    d->val_handle = &c->val_handle;
    d->flags = (c->cfg.is_write ? BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP : 0) |
               (c->cfg.is_indicate ? BLE_GATT_CHR_F_INDICATE : 0) | (c->cfg.is_notify ? BLE_GATT_CHR_F_NOTIFY : 0);
    if (c->desc != NULL) {
      struct ble_gatt_dsc_def* dsc = calloc(2, sizeof(*dsc));
      ERR_CHECK(dsc != NULL, ERR_BLE_NO_MEM, .uuid = c->cfg.uuid, .size = 2 * sizeof(*dsc));
      d->descriptors = dsc;
      dsc[0].uuid = new_uuid16(UUID_USER_DESC);
      ERR_CHECK(dsc[0].uuid != NULL, ERR_BLE_NO_MEM, .uuid = c->cfg.uuid, .size = sizeof(ble_uuid16_t));
      dsc[0].att_flags = BLE_ATT_F_READ;
      dsc[0].access_cb = dsc_access;
      dsc[0].arg = c->desc;
    }
  }
  return NULL;
}

// On failure the partial definition stays in `def`; sys_ble_free_svc_defs frees it.
err_h sys_ble_compile_svc(struct ble_gatt_svc_def* def, sys_ble_svc_node_t* s) {
  def->type = s->cfg.is_primary ? BLE_GATT_SVC_TYPE_PRIMARY : BLE_GATT_SVC_TYPE_SECONDARY;
  def->uuid = new_uuid16(s->cfg.uuid);
  ERR_CHECK(def->uuid != NULL, ERR_BLE_NO_MEM, .uuid = s->cfg.uuid, .size = sizeof(ble_uuid16_t));
  struct ble_gatt_chr_def* chars = NULL;
  err_h err = compile_chars(&chars, s);
  def->characteristics = chars;
  return err;
}

void sys_ble_free_svc_defs(struct ble_gatt_svc_def* svcs) {
  if (svcs == NULL) {
    return;
  }
  // Terminated by a zero entry; a partly compiled entry has a NULL uuid where it stopped.
  for (size_t i = 0; svcs[i].type != 0; i++) {
    free((void*)svcs[i].uuid);
    struct ble_gatt_chr_def* chars = (struct ble_gatt_chr_def*)svcs[i].characteristics;
    for (size_t j = 0; chars != NULL && chars[j].uuid != NULL; j++) {
      free((void*)chars[j].uuid);
      struct ble_gatt_dsc_def* dsc = chars[j].descriptors;
      if (dsc != NULL) {
        free((void*)dsc[0].uuid);
        free(dsc);
      }
    }
    free(chars);
  }
  free(svcs);
}

/* ----------------------------------------------------------------------------
 * Stack
 * ------------------------------------------------------------------------- */

static void on_reset(int reason) {
  ESP_LOGW(TAG, "NimBLE reset, reason %d", reason);
}

static void on_sync(void) {
  int rc = ble_hs_util_ensure_addr(0);
  if (rc == 0) {
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
  }
  if (rc != 0) {
    sys_ble_report(ERR_NEW(ERR_BLE_STACK, .rc = rc));
    return;
  }
  sys_ble_report(adv_start());
  ESP_LOGI(TAG, "advertising as \"%s\"", ble_svc_gap_device_name());
}

static void host_task(void* param) {
  (void)param;
  nimble_port_run();
  nimble_port_freertos_deinit();
}

static void ble_task(void* param) {
  (void)param;
  while (1) {
    xSemaphoreTake(sys_ble_tx_sem, sys_ble_task_wait());
    sys_ble_task_step(s_tx_scratch, sizeof(s_tx_scratch));
  }
}

err_h sys_ble_stack_start(struct ble_gatt_svc_def* svcs) {
  esp_err_t nvs = nvs_flash_init();
  if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ERR_TRY_ESP(nvs_flash_erase());
    nvs = nvs_flash_init();
  }
  ERR_TRY_ESP(nvs);
  ERR_TRY_ESP(nimble_port_init());

  ble_hs_cfg.reset_cb = on_reset;
  ble_hs_cfg.sync_cb = on_sync;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
  ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
  ble_hs_cfg.sm_bonding = 0;
  ble_hs_cfg.sm_mitm = 0;
  ble_hs_cfg.sm_sc = 0;
  ble_store_config_init();

  ble_svc_gap_init();
  ble_svc_gatt_init();
  int rc = ble_svc_gap_device_name_set(CONFIG_SYS_BLE_DEVICE_NAME);
  ERR_CHECK(rc == 0, ERR_BLE_STACK, .rc = rc);
  // Preferred MTU: CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU (applied by NimBLE at init).
  rc = ble_gatts_count_cfg(svcs);
  ERR_CHECK(rc == 0, ERR_BLE_STACK, .rc = rc);
  rc = ble_gatts_add_svcs(svcs);
  ERR_CHECK(rc == 0, ERR_BLE_STACK, .rc = rc);

  nimble_port_freertos_init(host_task);
  if (sys_ble_task_h == NULL) {
    U_TASK_START_ON_CORE(sys_ble_task_h, ble_task, NULL, CONFIG_SYS_BLE_TASK_PRIO, 0);
  }
  return NULL;
}

err_h sys_ble_stack_add_svc(sys_ble_svc_node_t* s, struct ble_gatt_svc_def* svcs) {
  int rc = ble_gatts_add_dynamic_svcs(svcs);  // NimBLE sends Service Changed itself
  ERR_CHECK(rc == 0, ERR_BLE_GATT, .uuid = s->cfg.uuid, .rc = rc);
  return NULL;
}

err_h sys_ble_stack_delete_svc(uint16_t uuid) {
  ble_uuid16_t u = BLE_UUID16_INIT(uuid);
  int rc = ble_gatts_delete_svc(&u.u);
  ERR_CHECK(rc == 0, ERR_BLE_GATT, .uuid = uuid, .rc = rc);
  return NULL;
}

int sys_ble_stack_notify(uint16_t conn_handle, uint16_t val_handle, const uint8_t* data, size_t len, bool indicate) {
  struct os_mbuf* om = ble_hs_mbuf_from_flat(data, (uint16_t)len);
  if (om == NULL) {
    return BLE_HS_ENOMEM;
  }
  return indicate ? ble_gatts_indicate_custom(conn_handle, val_handle, om)
                  : ble_gatts_notify_custom(conn_handle, val_handle, om);
}
