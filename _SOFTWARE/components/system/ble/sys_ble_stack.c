#include "sys_error.h"
#include "sys_ble_priv.h"

static const char* TAG = __FILE_NAME__;

R_TASK_DEFINE(m_ble_task, BLE_TASK_STACK_SIZE);

/* GAP Advertising State */
static uint8_t own_addr_type;
static bool adv_configured = false;
static uint8_t addr_val[6] = {0};

static int ble_gap_configure_advertising(void);
static int ble_gap_advertising_start(void);
static int gap_event_handler(struct ble_gap_event* event, void* arg);
static int sys_ble_gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg);
static int sys_ble_gatt_dsc_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg);
static void sys_ble_task_func(void* pvParameters);

void ble_store_config_init(void);

/*****************************************************************************************/
/* GAP Advertising Engine                                                                */
/*****************************************************************************************/

static int ble_gap_configure_advertising(void) {
  const char* name = ble_svc_gap_device_name();
  struct ble_hs_adv_fields adv_fields = {0};

  adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  adv_fields.name = (uint8_t*)name;
  adv_fields.name_len = strlen(name);
  adv_fields.name_is_complete = 1;
  adv_fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
  adv_fields.tx_pwr_lvl_is_present = 1;
  adv_fields.appearance = BLE_GAP_APPEARANCE_GENERIC_TAG;
  adv_fields.appearance_is_present = 1;

  int rc = ble_gap_adv_set_fields(&adv_fields);
  if (rc != 0) {
    return rc;
  }
  adv_configured = true;
  return 0;
}

static int ble_gap_advertising_start(void) {
  if (!adv_configured) {
    int rc = ble_gap_configure_advertising();
    if (rc != 0) return rc;
  }

  struct ble_gap_adv_params adv_params = {0};
  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
  adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(100);
  adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(150);

  return ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, gap_event_handler, NULL);
}

#define OWNER OWNER_SYS_BLE_ADV
err_h sys_ble_advertising_init(void) {
  CHECK_BLE_CALL(ble_hs_util_ensure_addr(0));
  CHECK_BLE_CALL(ble_hs_id_infer_auto(0, &own_addr_type));
  CHECK_BLE_CALL(ble_hs_id_copy_addr(own_addr_type, addr_val, NULL));
  CHECK_BLE_CALL(ble_gap_configure_advertising());
  CHECK_BLE_CALL(ble_gap_advertising_start());
  ESP_LOGI(TAG, "BLE Advertising started, own_addr_type=%d, MAC: %02X:%02X:%02X:%02X:%02X:%02X", own_addr_type, addr_val[5], addr_val[4], addr_val[3], addr_val[2], addr_val[1], addr_val[0]);
  return NULL;
}

err_h sys_ble_reconfigure_advertising(void) {
  adv_configured = false;
  if (!ble_hs_synced()) return NULL;

  if (ble_gap_adv_active()) {
    int rc = ble_gap_adv_stop();
    if (rc != 0 && rc != BLE_HS_EALREADY) {
      SE_RET_ERR(ERR_BLE_ADV_FAILED, rc);
    }
  }
  return sys_ble_advertising_init();
}
#undef OWNER

/*****************************************************************************************/
/* NimBLE Event Handlers                                                                 */
/*****************************************************************************************/

static void sys_ble_on_subscribe(uint16_t conn_handle, uint16_t attr_handle, bool indicate, bool notify) {
  sys_ble_char_node_t* target_char = NULL;

  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
  sys_ble_svc_node_t* s;
  LL_FOREACH(g_ble_ctx.services, s) {
    sys_ble_char_node_t* c;
    LL_FOREACH(s->chars, c) {
      if (c->val_handle == attr_handle) {
        target_char = c;
        break;
      }
    }
    if (target_char) break;
  }

  uint16_t char_uuid = target_char ? target_char->cfg.uuid : 0;
  R_MUTEX_UNLOCK(sys_ble_mutex);

  ESP_LOGI(TAG, "Client subscription updated: uuid=0x%04X conn_handle=%d indicate=%d notify=%d", char_uuid, conn_handle, indicate, notify);
}

static int gap_event_handler(struct ble_gap_event* event, void* arg) {
  struct ble_gap_conn_desc desc;

  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status == 0) {
        ESP_LOGI(TAG, "Connected: conn_handle=%d", event->connect.conn_handle);
        R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
        g_ble_ctx.conn_handle = event->connect.conn_handle;
        g_ble_ctx.is_connected = true;
        R_MUTEX_UNLOCK(sys_ble_mutex);

        xSemaphoreGive(sys_ble_tx_sem);
        SYS_BLE_CB(SYS_BLE_EVENT_CONNECT, event->connect.conn_handle, g_ble_ctx.route_masks[SYS_BLE_EVENT_CONNECT]);
      } else {
        ESP_LOGW(TAG, "Connection failed: err = %d", event->connect.status);
        SYS_BLE_CB(SYS_BLE_EVENT_FAILURE, ESP_FAIL, g_ble_ctx.route_masks[SYS_BLE_EVENT_FAILURE]);
        sys_ble_advertising_init();
      }
      return 0;

    case BLE_GAP_EVENT_DISCONNECT: {
      int reason = event->disconnect.reason;
      if (reason >= 0x0200 && reason <= 0x02FF) {
        ESP_LOGI(TAG, "Disconnected: HCI Reason=0x%02X", (reason - 0x0200));
      } else {
        ESP_LOGI(TAG, "Disconnected: Host Reason=%d", reason);
      }

      R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
      g_ble_ctx.conn_handle = 0;
      g_ble_ctx.is_connected = false;
      R_MUTEX_UNLOCK(sys_ble_mutex);

      SYS_BLE_CB(SYS_BLE_EVENT_DISCONNECT, reason, g_ble_ctx.route_masks[SYS_BLE_EVENT_DISCONNECT]);
      sys_ble_reconfigure_advertising();
      break;
    }

    case BLE_GAP_EVENT_CONN_UPDATE:
      return ble_gap_conn_find(event->conn_update.conn_handle, &desc);

    case BLE_GAP_EVENT_ADV_COMPLETE:
      sys_ble_advertising_init();
      return 0;

    case BLE_GAP_EVENT_NOTIFY_TX:
      return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
      sys_ble_on_subscribe(event->subscribe.conn_handle, event->subscribe.attr_handle, event->subscribe.cur_indicate, event->subscribe.cur_notify);
      return 0;

    case BLE_GAP_EVENT_MTU:
      R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
      g_ble_ctx.mtu_size = event->mtu.value;
      R_MUTEX_UNLOCK(sys_ble_mutex);
      ESP_LOGI(TAG, "MTU updated: conn_handle=%d mtu=%d", event->mtu.conn_handle, event->mtu.value);
      return 0;
  }
  return 0;
}

/*****************************************************************************************/
/* Stack Initialization & Task Management                                                */
/*****************************************************************************************/

static void on_stack_reset(int reason) {
  ESP_LOGW(TAG, "NimBLE stack reset reason: %d", reason);
}

static void on_stack_sync(void) {
  ESP_LOGI(TAG, "NimBLE host synced with controller, starting advertising...");
  err_h err = sys_ble_advertising_init();
  if (err) SE_push_to_handler(err);
}

static void nimble_host_config_init(void) {
  ble_hs_cfg.reset_cb = on_stack_reset;
  ble_hs_cfg.sync_cb = on_stack_sync;
  ble_hs_cfg.gatts_register_cb = NULL;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

  ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_mitm = 0;
  ble_hs_cfg.sm_sc = 1;
  ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

  ble_store_config_init();
}

static void nimble_host_task(void* param) {
  (void)param;
  ESP_LOGI(TAG, "BLE Host Task Started");
  nimble_port_run();
  nimble_port_freertos_deinit();
}

#define OWNER OWNER_SYS_BLE_STACK
err_h sys_ble_set_name(const char* name) {
  SE_CHECK_NOT_NULL(name);
  int res = ble_svc_gap_device_name_set(name);
  if (res == 0) {
    if (ble_hs_synced()) {
      sys_ble_reconfigure_advertising();
    }
    ESP_LOGI(TAG, "Name set to %s", name);
    return NULL;
  }
  ESP_LOGE(TAG, "Failed to set BLE device name, res=%d", res);
  SE_RET_ERR(ERR_BLE_STACK_FAILED, res);
}

err_h sys_ble_stack_init(struct ble_gatt_svc_def* svcs) {
  SE_RET_IF_ESP_ERR(nvs_flash_init());
  SE_RET_IF_ESP_ERR(nimble_port_init());

  nimble_host_config_init();

  ble_svc_gap_init();
  ble_svc_gatt_init();
  ble_att_set_preferred_mtu(BLE_ATT_MTU_MAX);

  CHECK_BLE_CALL(ble_gatts_count_cfg(svcs));
  CHECK_BLE_CALL(ble_gatts_add_svcs(svcs));

  nimble_port_freertos_init(nimble_host_task);
  sys_ble_set_name("runit");

  R_TASK_START_ON_CORE(m_ble_task, &sys_ble_task_func, &g_ble_ctx, CONFIG_PRIORITY_BLE_MANAGER_TASK, 0);
  return NULL;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_SEND
err_h sys_ble_send_raw(uint16_t conn_handle, uint16_t chr_val_handle, const uint8_t* data, size_t len, bool indicate) {
  SE_CHECK_NOT_NULL(data);
  if (len == 0) return NULL;

  struct os_mbuf* om = ble_hs_mbuf_from_flat(data, len);
  SE_CHECK_IF_ALLOCATED(om);

  int rc = indicate ? ble_gatts_indicate_custom(conn_handle, chr_val_handle, om) : ble_gatts_notify_custom(conn_handle, chr_val_handle, om);

  if (rc != 0) {
    if (rc == BLE_HS_ENOMEM) SE_RET_ERR(ERR_BASE_NO_MEM, rc);
    if (rc == BLE_HS_ENOTCONN) SE_RET_ERR(ERR_BASE_INVALID_STATE, 0);
    SE_RET_ERR(ERR_BLE_STACK_FAILED, rc);
  }
  return NULL;
}
#undef OWNER

/*****************************************************************************************/
/* Access Callbacks & GATT Compiler                                                     */
/*****************************************************************************************/

static int sys_ble_gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg) {
  sys_ble_char_node_t* char_node = (sys_ble_char_node_t*)arg;
  if (!char_node) return BLE_ATT_ERR_UNLIKELY;

  if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
    size_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len > 0) {
      if (!char_node->rx_buff) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
      uint8_t data_buffer[512];
      size_t copy_len = len > sizeof(data_buffer) ? sizeof(data_buffer) : len;
      os_mbuf_copydata(ctxt->om, 0, copy_len, data_buffer);

      if (xRingbufferSend(char_node->rx_buff, data_buffer, copy_len, 0) != pdTRUE) {
        ESP_LOGW(TAG, "RX buffer overflow on char uuid 0x%04X", char_node->cfg.uuid);
        R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
        g_ble_ctx.rx_overflow_count++;
        R_MUTEX_UNLOCK(sys_ble_mutex);
        SYS_BLE_CB(SYS_BLE_EVENT_FAILURE, ESP_FAIL, g_ble_ctx.route_masks[SYS_BLE_EVENT_FAILURE]);
      } else {
        xSemaphoreGive(char_node->rx_sem);
      }
    }
    return 0;
  } else if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
    return 0;
  }
  return BLE_ATT_ERR_UNLIKELY;
}

static int sys_ble_gatt_dsc_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg) {
  const char* desc = (const char*)arg;
  if (!desc) return BLE_ATT_ERR_UNLIKELY;
  return os_mbuf_append(ctxt->om, desc, strlen(desc)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static ble_uuid_t* malloc_uuid(uint16_t uuid16) {
  ble_uuid16_t* u16 = malloc(sizeof(ble_uuid16_t));
  if (u16) {
    u16->u.type = BLE_UUID_TYPE_16;
    u16->value = uuid16;
  }
  return (ble_uuid_t*)u16;
}

static struct ble_gatt_chr_def* compile_chars(const sys_ble_char_node_t* chars_head, bool* out_success) {
  *out_success = true;
  uint16_t count = 0;
  const sys_ble_char_node_t* c;
  LL_FOREACH((sys_ble_char_node_t*)chars_head, c) {
    count++;
  }

  struct ble_gatt_chr_def* chr_defs = calloc(count + 1, sizeof(struct ble_gatt_chr_def));
  if (!chr_defs) {
    *out_success = false;
    return NULL;
  }

  int idx = 0;
  LL_FOREACH((sys_ble_char_node_t*)chars_head, c) {
    chr_defs[idx].uuid = malloc_uuid(c->cfg.uuid);
    chr_defs[idx].access_cb = sys_ble_gatt_access_cb;
    chr_defs[idx].arg = (void*)c;
    chr_defs[idx].val_handle = (uint16_t*)&c->val_handle;

    uint16_t flags = 0;
    if (c->cfg.is_read) flags |= BLE_GATT_CHR_F_READ;
    if (c->cfg.is_write) flags |= BLE_GATT_CHR_F_WRITE;
    if (c->cfg.is_indicate) flags |= BLE_GATT_CHR_F_INDICATE;
    if (c->cfg.is_notify) flags |= BLE_GATT_CHR_F_NOTIFY;
    chr_defs[idx].flags = flags;

    if (c->cfg.desc && strlen(c->cfg.desc) > 0) {
      struct ble_gatt_dsc_def* dsc_defs = calloc(2, sizeof(struct ble_gatt_dsc_def));
      if (dsc_defs) {
        dsc_defs[0].uuid = malloc_uuid(0x2901);
        dsc_defs[0].att_flags = BLE_ATT_F_READ;
        dsc_defs[0].access_cb = sys_ble_gatt_dsc_cb;
        dsc_defs[0].arg = (void*)c->cfg.desc;
        chr_defs[idx].descriptors = dsc_defs;
      }
    }
    idx++;
  }
  return chr_defs;
}

#define OWNER OWNER_SYS_BLE_SERVICE_CREATE
err_h populate_svc_def(struct ble_gatt_svc_def* svc_def, const sys_ble_svc_node_t* s) {
  SE_CHECK_NOT_NULL(svc_def);
  SE_CHECK_NOT_NULL(s);

  svc_def->type = s->cfg.is_primary ? BLE_GATT_SVC_TYPE_PRIMARY : BLE_GATT_SVC_TYPE_SECONDARY;
  svc_def->uuid = malloc_uuid(s->cfg.uuid);
  SE_CHECK_IF_ALLOCATED(svc_def->uuid);

  bool ok = false;
  svc_def->characteristics = compile_chars(s->chars, &ok);
  if (!ok) SE_RET_ERR(ERR_BASE_NO_MEM, s->cfg.uuid);
  return NULL;
}
#undef OWNER

void sys_ble_free_compiled_gatt_db(struct ble_gatt_svc_def* svcs) {
  if (!svcs) return;

  for (int i = 0; svcs[i].type != BLE_GATT_SVC_TYPE_END; i++) {
    if (svcs[i].uuid) free((void*)svcs[i].uuid);

    if (svcs[i].characteristics) {
      struct ble_gatt_chr_def* chars = (struct ble_gatt_chr_def*)svcs[i].characteristics;
      for (int j = 0; chars[j].uuid != NULL; j++) {
        if (chars[j].uuid) free((void*)chars[j].uuid);

        if (chars[j].descriptors) {
          struct ble_gatt_dsc_def* dscs = (struct ble_gatt_dsc_def*)chars[j].descriptors;
          for (int k = 0; dscs[k].uuid != NULL; k++) {
            if (dscs[k].uuid) free((void*)dscs[k].uuid);
          }
          free((void*)chars[j].descriptors);
        }
      }
      free((void*)svcs[i].characteristics);
    }
  }
  free(svcs);
}

/*****************************************************************************************/
/* TX Task & Dequeue Worker                                                              */
/*****************************************************************************************/

static bool try_send_slot(sys_ble_tx_slot_t* slot, sys_ble_char_node_t* c, size_t max_payload, uint8_t* tx_data) {
  size_t tx_len = 0;
  err_h deq_res = sys_buff_prepare_tx(&slot->tx_buff, tx_data, max_payload, &tx_len);
  if ((deq_res != NULL) || tx_len == 0) return false;

  uint16_t conn_handle;
  uint16_t val_handle;

  R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
  conn_handle = g_ble_ctx.conn_handle;
  val_handle = c->val_handle;
  R_MUTEX_UNLOCK(sys_ble_mutex);

  err_h send_sta;
  do {
    send_sta = sys_ble_send_raw(conn_handle, val_handle, tx_data, tx_len, slot->is_indication);
    if (send_sta != NULL && send_sta->tag == ERR_BASE_NO_MEM) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  } while (send_sta != NULL && send_sta->tag == ERR_BASE_NO_MEM && g_ble_ctx.is_connected);

  if (send_sta == NULL) {
    return true;
  } else if (send_sta->tag != ERR_BASE_NO_MEM) {
    SYS_BLE_CB(SYS_BLE_EVENT_FAILURE, send_sta->tag, g_ble_ctx.route_masks[SYS_BLE_EVENT_FAILURE]);
  }
  return false;
}

static void sys_ble_task_func(void* pvParameters) {
  (void)pvParameters;
  uint8_t tx_data[527];

  while (1) {
    xSemaphoreTake(sys_ble_tx_sem, portMAX_DELAY);
    bool data_sent;

    do {
      data_sent = false;
      memset(tx_data, 0, sizeof(tx_data));

      R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
      uint16_t current_mtu = g_ble_ctx.mtu_size;
      bool connected = g_ble_ctx.is_connected;
      int tx_slot_count = g_ble_ctx.tx_slot_count;
      R_MUTEX_UNLOCK(sys_ble_mutex);

      size_t max_payload = (current_mtu > 3) ? (current_mtu - 3) : 20;
      if (max_payload > sizeof(tx_data)) max_payload = sizeof(tx_data);
      if (!connected) break;

      for (int i = 0; i < tx_slot_count; i++) {
        sys_ble_tx_slot_t* slot = g_ble_ctx.tx_slots[i].slot;
        sys_ble_char_node_t* c = g_ble_ctx.tx_slots[i].chr;
        if (!slot || !slot->tx_buff.buff || !c->val_handle) continue;

        if (try_send_slot(slot, c, max_payload, tx_data)) {
          data_sent = true;
          break;
        }
      }
    } while (data_sent);
  }
}
