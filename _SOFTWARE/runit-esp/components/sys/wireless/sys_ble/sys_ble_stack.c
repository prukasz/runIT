#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "sdkconfig.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "sys_ble_internal.h"
#include "utils.h"

#define TAG __FILE_NAME__

#define ADV_ITVL_MIN_MS    20u      // the fastest advertising the spec allows: quick (re)connects
#define ADV_ITVL_MAX_MS    30u
#define APPEARANCE         0x0200u  // Generic Tag
#define CONN_ITVL_UNITS    6u       // 7.5 ms (1.25 ms units): the spec minimum, the lowest latency
#define CONN_TIMEOUT_UNITS 200u     // 2 s supervision timeout (10 ms units)
#define UUID_USER_DESC     0x2901u

static uint8_t s_own_addr_type;
static ble_uuid16_t s_adv_service;  // in the advertisement: a client filters by it

static int gap_event(struct ble_gap_event* event, void* arg);

static void adv_start(void) {
  const char* name = ble_svc_gap_device_name();
  const struct ble_hs_adv_fields fields = {
      .uuids16 = &s_adv_service,
      .num_uuids16 = 1,
      .uuids16_is_complete = 1,
      .flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,
      .name = (const uint8_t*)name,
      .name_len = (uint8_t)strlen(name),
      .name_is_complete = 1,
      .tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO,
      .tx_pwr_lvl_is_present = 1,
      .appearance = APPEARANCE,
      .appearance_is_present = 1,
  };
  const struct ble_gap_adv_params params = {
      .conn_mode = BLE_GAP_CONN_MODE_UND,
      .disc_mode = BLE_GAP_DISC_MODE_GEN,
      .itvl_min = BLE_GAP_ADV_ITVL_MS(ADV_ITVL_MIN_MS),
      .itvl_max = BLE_GAP_ADV_ITVL_MS(ADV_ITVL_MAX_MS),
  };
  int rc = ble_gap_adv_set_fields(&fields);
  if (rc == 0) {
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
  }
  if (rc != 0 && rc != BLE_HS_EALREADY) {
    err_report(ERR_NEW(ERR_BLE_ADV, .rc = rc));  // the board is invisible until the next restart of advertising
  }
}

/* The board asks for the connection parameters only. The MTU exchange (a GATT client's), the PHY and the packet length
 * are the central's: a request from here collides with its own and can leave a procedure unanswered, and the link
 * drops at 40 s (SYS_BLE.MD). */
static void on_connect(uint16_t conn) {
  // A client that starts the MTU exchange at once can finish it before this event: the link's MTU now.
  const uint16_t mtu = ble_att_mtu(conn);
  sys_ble_on_connect(conn, mtu != 0 ? mtu : BLE_ATT_MTU_DFLT);
  const struct ble_gap_upd_params upd = {
      .itvl_min = CONN_ITVL_UNITS,
      .itvl_max = CONN_ITVL_UNITS,
      .supervision_timeout = CONN_TIMEOUT_UNITS,
  };
  const int rc = ble_gap_update_params(conn, &upd);  // the central may refuse them or round them up
  if (rc != 0) {
    ESP_LOGW(TAG, "connection parameters: %d", rc);
  }
  ESP_LOGI(TAG, "connected (handle %u, MTU %u)", conn, mtu);
}

static int gap_event(struct ble_gap_event* event, void* arg) {
  (void)arg;
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status == 0) {
        on_connect(event->connect.conn_handle);
      } else {
        adv_start();
      }
      return 0;
    case BLE_GAP_EVENT_DISCONNECT:
      ESP_LOGI(TAG, "disconnected (reason 0x%x)", event->disconnect.reason);
      sys_ble_on_disconnect();
      adv_start();
      return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
      adv_start();
      return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
      sys_ble_on_subscribe(event->subscribe.attr_handle, event->subscribe.cur_notify || event->subscribe.cur_indicate);
      return 0;
    case BLE_GAP_EVENT_MTU:
      ESP_LOGI(TAG, "MTU %u", event->mtu.value);
      sys_ble_on_mtu(event->mtu.value);
      return 0;
    default:
      return 0;
  }
}

/* ----------------------------------------------------------------------------
 * GATT access (host task)
 * ------------------------------------------------------------------------- */

static int chr_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt* ctxt, void* arg) {
  (void)conn, (void)attr;
  if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  uint8_t data[BLE_ATT_ATTR_MAX_LEN];
  const uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
  if (len == 0) {
    return 0;
  }
  if (len > sizeof(data)) {
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  }
  os_mbuf_copydata(ctxt->om, 0, len, data);
  return sys_ble_on_write(arg, data, len);
}

static int dsc_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt* ctxt, void* arg) {
  (void)conn, (void)attr;
  return os_mbuf_append(ctxt->om, arg, strlen(arg)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

/* ----------------------------------------------------------------------------
 * The table's definitions (NimBLE keeps pointing at them)
 * ------------------------------------------------------------------------- */

static ble_uuid_t* new_uuid16(uint16_t value) {
  ble_uuid16_t* u = malloc(sizeof(*u));
  if (u != NULL) {
    *u = (ble_uuid16_t)BLE_UUID16_INIT(value);
  }
  return (ble_uuid_t*)u;
}

err_h sys_ble_compile(struct ble_gatt_svc_def* def, ble_svc_t* s) {
  def->type = s->primary ? BLE_GATT_SVC_TYPE_PRIMARY : BLE_GATT_SVC_TYPE_SECONDARY;
  def->uuid = new_uuid16(s->uuid);
  SYS_BLE_CHECK_MEM(def->uuid != NULL, s->uuid, sizeof(ble_uuid16_t));
  size_t count = 0;
  ble_char_t* c;
  LL_FOREACH(s->chars, c) {
    count++;
  }
  struct ble_gatt_chr_def* chars = calloc(count + 1, sizeof(*chars));
  def->characteristics = chars;
  SYS_BLE_CHECK_MEM(chars != NULL, s->uuid, (count + 1) * sizeof(*chars));
  struct ble_gatt_chr_def* d = chars;
  LL_FOREACH(s->chars, c) {
    d->uuid = new_uuid16(c->uuid);
    SYS_BLE_CHECK_MEM(d->uuid != NULL, c->uuid, sizeof(ble_uuid16_t));
    d->access_cb = chr_access;
    d->arg = c;
    d->val_handle = &c->val_handle;
    d->flags = ((c->flags & SYS_BLE_CHR_WRITE) ? BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP : 0) |
               ((c->flags & SYS_BLE_CHR_INDICATE) ? BLE_GATT_CHR_F_INDICATE : 0) |
               ((c->flags & SYS_BLE_CHR_NOTIFY) ? BLE_GATT_CHR_F_NOTIFY : 0);
    if (c->name != NULL) {
      struct ble_gatt_dsc_def* dsc = calloc(2, sizeof(*dsc));
      d->descriptors = dsc;
      SYS_BLE_CHECK_MEM(dsc != NULL, c->uuid, 2 * sizeof(*dsc));
      dsc[0].uuid = new_uuid16(UUID_USER_DESC);
      SYS_BLE_CHECK_MEM(dsc[0].uuid != NULL, c->uuid, sizeof(ble_uuid16_t));
      dsc[0].att_flags = BLE_ATT_F_READ;
      dsc[0].access_cb = dsc_access;
      dsc[0].arg = c->name;
    }
    d++;
  }
  return NULL;
}

// Ended by a zero entry; a partly compiled entry has a NULL uuid where it stopped.
void sys_ble_free_defs(struct ble_gatt_svc_def* svcs) {
  for (size_t i = 0; svcs != NULL && svcs[i].type != 0; i++) {
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
 * The stack
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
    err_report(ERR_NEW(ERR_BLE_STACK, .rc = rc));
    return;
  }
  adv_start();
  ESP_LOGI(TAG, "advertising as \"%s\"", ble_svc_gap_device_name());
}

static void host_task(void* arg) {
  (void)arg;
  nimble_port_run();
  nimble_port_freertos_deinit();
}

err_h sys_ble_stack_init(uint16_t adv_service) {
  s_adv_service = (ble_uuid16_t)BLE_UUID16_INIT(adv_service);
  ERR_TRY_ESP(nimble_port_init());
  ble_hs_cfg.reset_cb = on_reset;
  ble_hs_cfg.sync_cb = on_sync;
  ble_svc_gap_init();
  ble_svc_gatt_init();
  const int rc = ble_svc_gap_device_name_set(CONFIG_SYS_BLE_DEVICE_NAME);
  ERR_CHECK(rc == 0, ERR_BLE_STACK, .rc = rc);
  return NULL;
}

err_h sys_ble_stack_add_boot(struct ble_gatt_svc_def* svcs) {
  int rc = ble_gatts_count_cfg(svcs);
  if (rc == 0) {
    rc = ble_gatts_add_svcs(svcs);
  }
  ERR_CHECK(rc == 0, ERR_BLE_STACK, .rc = rc);
  return NULL;
}

err_h sys_ble_stack_run(void) {
  nimble_port_freertos_init(host_task);
  return NULL;
}

err_h sys_ble_stack_add(uint16_t uuid, struct ble_gatt_svc_def* svcs) {
  const int rc = ble_gatts_add_dynamic_svcs(svcs);
  ERR_CHECK(rc == 0, ERR_BLE_GATT, .uuid = uuid, .rc = rc);
  return NULL;
}

err_h sys_ble_stack_delete(uint16_t uuid) {
  const ble_uuid16_t u = BLE_UUID16_INIT(uuid);
  const int rc = ble_gatts_delete_svc(&u.u);
  ERR_CHECK(rc == 0, ERR_BLE_GATT, .uuid = uuid, .rc = rc);
  return NULL;
}

int sys_ble_stack_notify(uint16_t conn, uint16_t val_handle, const uint8_t* data, uint32_t len, bool indicate) {
  struct os_mbuf* om = ble_hs_mbuf_from_flat(data, (uint16_t)len);
  if (om == NULL) {
    return BLE_HS_ENOMEM;
  }
  return indicate ? ble_gatts_indicate_custom(conn, val_handle, om) : ble_gatts_notify_custom(conn, val_handle, om);
}

int sys_ble_stack_terminate(uint16_t conn) {
  return ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
}
