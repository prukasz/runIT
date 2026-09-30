#include "sys_data_connector.h"
#include <string.h>
#include "utils.h"

#undef OWNER
#define OWNER OWNER_SYS_DATA_CONNECTOR_BASE

static const char* TAG = "sys_data_connector";

// -----------------------------------------------------------------------------
// Registries
// -----------------------------------------------------------------------------

typedef struct {
  uint8_t provider_id;
  uint32_t endpoint;
} binding_t;

typedef struct {
  bool allocated;
  bool suspended;
  bool system;
  uint8_t header;
  uint16_t max_frame; /* incl. stream byte, <= CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX */
  char name[CONFIG_SYS_DATA_CONNECTOR_NAME_MAX];
  uint8_t tx_count;
  binding_t tx[CONFIG_SYS_DATA_CONNECTOR_PROVIDERS_MAX];
  uint8_t rx_count;
  binding_t rx[CONFIG_SYS_DATA_CONNECTOR_PROVIDERS_MAX];
  uint8_t rx_next; /* receive() starts here, so one busy provider can't starve the others */
} connector_t;

typedef struct {
  uint8_t id;
  const sys_data_connector_provider_t* ops;
} provider_slot_t;

/* Providers are registered at boot and never removed, so lookups need no lock. */
static provider_slot_t s_providers[CONFIG_SYS_DATA_PROVIDER_MAX];

/* Guards s_connectors. Provider calls are made on copies taken under it, never
   while holding it, except bind_rx / unbind_rx (lock order: connector -> provider). */
R_MUTEX_DEFINE(sys_data_connector_mutex);
static connector_t s_connectors[CONFIG_SYS_DATA_CONNECTOR_MAX];

/* One wake semaphore per slot, static and never deleted: a consumer blocked on
   a slot keeps a valid handle while the connector is removed and recreated. */
static StaticSemaphore_t s_wake_storage[CONFIG_SYS_DATA_CONNECTOR_MAX];
static SemaphoreHandle_t s_wake[CONFIG_SYS_DATA_CONNECTOR_MAX];

static const sys_data_connector_provider_t* find_provider(uint8_t provider_id) {
  for (size_t i = 0; i < CONFIG_SYS_DATA_PROVIDER_MAX; i++) {
    if (s_providers[i].ops && s_providers[i].id == provider_id) return s_providers[i].ops;
  }
  return NULL;
}

/* Caller holds the mutex. NULL for an out-of-range or free slot. */
static connector_t* find_conn(uint8_t id) {
  if (id >= CONFIG_SYS_DATA_CONNECTOR_MAX || !s_connectors[id].allocated) return NULL;
  return &s_connectors[id];
}

static binding_t* find_binding(binding_t* list, uint8_t count, uint8_t provider_id) {
  for (uint8_t i = 0; i < count; i++) {
    if (list[i].provider_id == provider_id) return &list[i];
  }
  return NULL;
}

static void drop_binding(binding_t* list, uint8_t* count, binding_t* b) {
  for (binding_t* next = b + 1; next < list + *count; b++, next++) *b = *next;
  (*count)--;
}

// -----------------------------------------------------------------------------
// Providers
// -----------------------------------------------------------------------------

#undef OWNER
#define OWNER OWNER_SYS_DATA_CONNECTOR_REGISTER_PROVIDER
err_h sys_data_connector_register_provider(uint8_t provider_id, const sys_data_connector_provider_t* provider) {
  SE_CHECK_NOT_NULL(provider);
  if (provider_id == 0 || find_provider(provider_id)) {
    SE_FAIL(ERR_INVALID_VAL_UI32, .val = provider_id, .min = 1, .max = UINT8_MAX);
  }
  for (size_t i = 0; i < CONFIG_SYS_DATA_PROVIDER_MAX; i++) {
    if (!s_providers[i].ops) {
      s_providers[i] = (provider_slot_t){.id = provider_id, .ops = provider};
      ESP_LOGI(TAG, "Provider registered: %s (id=%u)", provider->name ? provider->name : "unnamed", provider_id);
      return NULL;
    }
  }
  SE_FAIL(ERR_BASE_NO_MEM, provider_id);
}

// -----------------------------------------------------------------------------
// Connector lifecycle
// -----------------------------------------------------------------------------

/* The app tells the outbound streams apart by their first byte alone. */
#define DC_STREAM_DISTINCT(a, b) _Static_assert((a) != (b), #a " and " #b " share a stream byte")
DC_STREAM_DISTINCT(CONFIG_TX_PACKET_CLASS_STATUS, CONFIG_TX_PACKET_CLASS_TELEMETRY);
DC_STREAM_DISTINCT(CONFIG_TX_PACKET_CLASS_STATUS, CONFIG_TX_PACKET_CLASS_LOGS);
DC_STREAM_DISTINCT(CONFIG_TX_PACKET_CLASS_STATUS, CONFIG_TX_PACKET_CLASS_ERRORS);
DC_STREAM_DISTINCT(CONFIG_TX_PACKET_CLASS_STATUS, CONFIG_TX_PACKET_CLASS_INTERFACE);
DC_STREAM_DISTINCT(CONFIG_TX_PACKET_CLASS_TELEMETRY, CONFIG_TX_PACKET_CLASS_LOGS);
DC_STREAM_DISTINCT(CONFIG_TX_PACKET_CLASS_TELEMETRY, CONFIG_TX_PACKET_CLASS_ERRORS);
DC_STREAM_DISTINCT(CONFIG_TX_PACKET_CLASS_TELEMETRY, CONFIG_TX_PACKET_CLASS_INTERFACE);
DC_STREAM_DISTINCT(CONFIG_TX_PACKET_CLASS_LOGS, CONFIG_TX_PACKET_CLASS_ERRORS);
DC_STREAM_DISTINCT(CONFIG_TX_PACKET_CLASS_LOGS, CONFIG_TX_PACKET_CLASS_INTERFACE);
DC_STREAM_DISTINCT(CONFIG_TX_PACKET_CLASS_ERRORS, CONFIG_TX_PACKET_CLASS_INTERFACE);
#undef DC_STREAM_DISTINCT

#undef OWNER
#define OWNER OWNER_SYS_DATA_CONNECTOR_INIT
err_h sys_data_connector_init(void) {
  for (size_t i = 0; i < CONFIG_SYS_DATA_CONNECTOR_MAX; i++) {
    if (!s_wake[i]) s_wake[i] = xSemaphoreCreateBinaryStatic(&s_wake_storage[i]);
  }

  static const sys_data_connector_cfg_t s_system_connectors[] = {
      {.id = SYS_DATA_CONNECTOR_LOGS, .name = "logs", .header = CONFIG_TX_PACKET_CLASS_LOGS, .system = true},
      {.id = SYS_DATA_CONNECTOR_ERRORS, .name = "errors", .header = CONFIG_TX_PACKET_CLASS_ERRORS, .system = true},
      {.id = SYS_DATA_CONNECTOR_TELEMETRY, .name = "telemetry", .header = CONFIG_TX_PACKET_CLASS_TELEMETRY, .system = true},
      {.id = SYS_DATA_CONNECTOR_INTERFACE, .name = "interface", .header = CONFIG_TX_PACKET_CLASS_INTERFACE, .system = true},
  };
  for (size_t i = 0; i < sizeof(s_system_connectors) / sizeof(s_system_connectors[0]); i++) {
    SE_TRY(sys_data_connector_create(&s_system_connectors[i]));
  }
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_DATA_CONNECTOR_CREATE
err_h sys_data_connector_create(const sys_data_connector_cfg_t* cfg) {
  SE_CHECK_NOT_NULL(cfg);
  if (cfg->id >= CONFIG_SYS_DATA_CONNECTOR_MAX) {
    SE_FAIL(ERR_INVALID_VAL_UI32, .val = cfg->id, .min = 0, .max = CONFIG_SYS_DATA_CONNECTOR_MAX - 1);
  }
  /* A frame is at least the stream byte plus one payload byte. */
  if (cfg->max_frame == 1 || cfg->max_frame > CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX) {
    SE_FAIL(ERR_INVALID_VAL_UI32, .val = cfg->max_frame, .min = 2, .max = CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX);
  }

  R_MUTEX_LOCK(sys_data_connector_mutex, WAIT_FOREVER);
  connector_t* c = &s_connectors[cfg->id];
  if (c->allocated && c->system) {
    R_MUTEX_UNLOCK(sys_data_connector_mutex);
    SE_FAIL(ERR_DATA_CONNECTOR_PROTECTED, .id = cfg->id);
  }
  bool created = !c->allocated;
  if (created) {
    memset(c, 0, sizeof(*c));
    c->allocated = true;
  }
  c->system = cfg->system;
  c->header = cfg->header;
  c->max_frame = cfg->max_frame ? cfg->max_frame : CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX;
  if (cfg->name && cfg->name[0] != '\0') {
    strncpy(c->name, cfg->name, sizeof(c->name) - 1);
    c->name[sizeof(c->name) - 1] = '\0';
  } else {
    snprintf(c->name, sizeof(c->name), "conn_%u", cfg->id);
  }
  R_MUTEX_UNLOCK(sys_data_connector_mutex);

  ESP_LOGI(TAG, "%s data connector: %s (id=%u, header=0x%02X, max_frame=%u)", created ? "Created" : "Reconfigured", c->name, cfg->id, cfg->header,
           c->max_frame);
  return NULL;
}

bool sys_data_connector_exists(uint8_t id) {
  R_MUTEX_LOCK(sys_data_connector_mutex, WAIT_FOREVER);
  bool exists = find_conn(id) != NULL;
  R_MUTEX_UNLOCK(sys_data_connector_mutex);
  return exists;
}

#undef OWNER
#define OWNER OWNER_SYS_DATA_CONNECTOR_REMOVE
err_h sys_data_connector_remove(uint8_t id) {
  R_MUTEX_LOCK(sys_data_connector_mutex, WAIT_FOREVER);
  connector_t* c = find_conn(id);
  if (!c) {
    R_MUTEX_UNLOCK(sys_data_connector_mutex);
    SE_FAIL(ERR_BASE_NOT_FOUND, id);
  }
  if (c->system) {
    R_MUTEX_UNLOCK(sys_data_connector_mutex);
    SE_FAIL(ERR_DATA_CONNECTOR_PROTECTED, .id = id);
  }

  /* Every provider is detached even if one fails; the first error is returned. */
  err_h first = NULL;
  for (uint8_t i = 0; i < c->rx_count; i++) {
    const sys_data_connector_provider_t* ops = find_provider(c->rx[i].provider_id);
    if (!ops || !ops->unbind_rx) continue;
    err_h err = ops->unbind_rx(c->rx[i].endpoint, id);
    if (err && !first) first = err;
    else SE_release(err);
  }
  memset(c, 0, sizeof(*c));
  R_MUTEX_UNLOCK(sys_data_connector_mutex);

  ESP_LOGI(TAG, "Removed data connector id=%u", id);
  SE_TRY(first);
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_DATA_CONNECTOR_SUSPEND
static SE_MUST_USE err_h set_suspended(uint8_t id, bool suspended) {
  R_MUTEX_LOCK(sys_data_connector_mutex, WAIT_FOREVER);
  connector_t* c = find_conn(id);
  if (!c) {
    R_MUTEX_UNLOCK(sys_data_connector_mutex);
    SE_FAIL(ERR_BASE_NOT_FOUND, id);
  }
  if (suspended && c->system && c->rx_count > 0) {
    R_MUTEX_UNLOCK(sys_data_connector_mutex);
    SE_FAIL(ERR_DATA_CONNECTOR_PROTECTED, .id = id);
  }
  c->suspended = suspended;
  R_MUTEX_UNLOCK(sys_data_connector_mutex);
  if (!suspended) sys_data_connector_notify_rx(id); /* frames that waited in the providers */
  return NULL;
}

err_h sys_data_connector_suspend(uint8_t id) {
  return set_suspended(id, true);
}

err_h sys_data_connector_resume(uint8_t id) {
  return set_suspended(id, false);
}

// -----------------------------------------------------------------------------
// Bindings
// -----------------------------------------------------------------------------

#undef OWNER
#define OWNER OWNER_SYS_DATA_CONNECTOR_BIND_TX
err_h sys_data_connector_bind_tx(uint8_t id, uint8_t provider_id, uint32_t endpoint) {
  const sys_data_connector_provider_t* ops = find_provider(provider_id);
  if (!ops || !ops->send) SE_FAIL(ERR_DATA_CONNECTOR_NO_PROVIDER, .provider_id = provider_id);

  R_MUTEX_LOCK(sys_data_connector_mutex, WAIT_FOREVER);
  connector_t* c = find_conn(id);
  if (!c) {
    R_MUTEX_UNLOCK(sys_data_connector_mutex);
    SE_FAIL(ERR_BASE_NOT_FOUND, id);
  }
  binding_t* b = find_binding(c->tx, c->tx_count, provider_id);
  if (!b) {
    if (c->tx_count >= CONFIG_SYS_DATA_CONNECTOR_PROVIDERS_MAX) {
      R_MUTEX_UNLOCK(sys_data_connector_mutex);
      SE_FAIL(ERR_BASE_NO_MEM, provider_id);
    }
    b = &c->tx[c->tx_count++];
    b->provider_id = provider_id;
  }
  b->endpoint = endpoint;
  R_MUTEX_UNLOCK(sys_data_connector_mutex);

  ESP_LOGI(TAG, "Connector %u: TX -> %s endpoint 0x%lX", id, ops->name ? ops->name : "unnamed", (unsigned long)endpoint);
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_DATA_CONNECTOR_UNBIND_TX
err_h sys_data_connector_unbind_tx(uint8_t id, uint8_t provider_id) {
  R_MUTEX_LOCK(sys_data_connector_mutex, WAIT_FOREVER);
  connector_t* c = find_conn(id);
  if (!c) {
    R_MUTEX_UNLOCK(sys_data_connector_mutex);
    SE_FAIL(ERR_BASE_NOT_FOUND, id);
  }
  binding_t* b = find_binding(c->tx, c->tx_count, provider_id);
  if (b && c->system && c->tx_count == 1) {
    R_MUTEX_UNLOCK(sys_data_connector_mutex);
    SE_FAIL(ERR_DATA_CONNECTOR_PROTECTED, .id = id);
  }
  if (b) drop_binding(c->tx, &c->tx_count, b);
  R_MUTEX_UNLOCK(sys_data_connector_mutex);
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_DATA_CONNECTOR_BIND_RX
err_h sys_data_connector_bind_rx(uint8_t id, uint8_t provider_id, uint32_t endpoint) {
  const sys_data_connector_provider_t* ops = find_provider(provider_id);
  if (!ops || !ops->dequeue) SE_FAIL(ERR_DATA_CONNECTOR_NO_PROVIDER, .provider_id = provider_id);

  R_MUTEX_LOCK(sys_data_connector_mutex, WAIT_FOREVER);
  connector_t* c = find_conn(id);
  if (!c) {
    R_MUTEX_UNLOCK(sys_data_connector_mutex);
    SE_FAIL(ERR_BASE_NOT_FOUND, id);
  }
  binding_t* b = find_binding(c->rx, c->rx_count, provider_id);
  if (!b && c->rx_count >= CONFIG_SYS_DATA_CONNECTOR_PROVIDERS_MAX) {
    R_MUTEX_UNLOCK(sys_data_connector_mutex);
    SE_FAIL(ERR_BASE_NO_MEM, provider_id);
  }
  if (b && b->endpoint == endpoint) {
    R_MUTEX_UNLOCK(sys_data_connector_mutex);
    return NULL;
  }

  /* Attach the new endpoint before detaching the old one, so a failed bind
     leaves the previous binding working. */
  if (ops->bind_rx) {
    err_h err = ops->bind_rx(endpoint, id);
    if (err) {
      R_MUTEX_UNLOCK(sys_data_connector_mutex);
      SE_TRY(err);
    }
  }
  err_h old_err = NULL;
  if (b) {
    if (ops->unbind_rx) old_err = ops->unbind_rx(b->endpoint, id);
  } else {
    b = &c->rx[c->rx_count++];
    b->provider_id = provider_id;
  }
  b->endpoint = endpoint;
  R_MUTEX_UNLOCK(sys_data_connector_mutex);

  ESP_LOGI(TAG, "Connector %u: RX <- %s endpoint 0x%lX", id, ops->name ? ops->name : "unnamed", (unsigned long)endpoint);
  SE_TRY(old_err);
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_DATA_CONNECTOR_UNBIND_RX
err_h sys_data_connector_unbind_rx(uint8_t id, uint8_t provider_id) {
  R_MUTEX_LOCK(sys_data_connector_mutex, WAIT_FOREVER);
  connector_t* c = find_conn(id);
  if (!c) {
    R_MUTEX_UNLOCK(sys_data_connector_mutex);
    SE_FAIL(ERR_BASE_NOT_FOUND, id);
  }
  binding_t* b = find_binding(c->rx, c->rx_count, provider_id);
  if (!b) {
    R_MUTEX_UNLOCK(sys_data_connector_mutex);
    return NULL;
  }
  if (c->system && c->rx_count == 1) {
    R_MUTEX_UNLOCK(sys_data_connector_mutex);
    SE_FAIL(ERR_DATA_CONNECTOR_PROTECTED, .id = id);
  }
  const sys_data_connector_provider_t* ops = find_provider(provider_id);
  err_h err = (ops && ops->unbind_rx) ? ops->unbind_rx(b->endpoint, id) : NULL;
  drop_binding(c->rx, &c->rx_count, b);
  c->rx_next = 0;
  R_MUTEX_UNLOCK(sys_data_connector_mutex);

  ESP_LOGI(TAG, "Connector %u: RX binding of provider %u removed", id, provider_id);
  SE_TRY(err);
  return NULL;
}

// -----------------------------------------------------------------------------
// TX
// -----------------------------------------------------------------------------

typedef struct {
  bool suspended;
  uint8_t header;
  uint16_t max_frame;
  uint8_t count;
  binding_t tx[CONFIG_SYS_DATA_CONNECTOR_PROVIDERS_MAX];
} tx_snapshot_t;

/* Copy what a send needs, so providers are called without the lock. */
static bool take_tx_snapshot(uint8_t id, tx_snapshot_t* snap) {
  R_MUTEX_LOCK(sys_data_connector_mutex, WAIT_FOREVER);
  const connector_t* c = find_conn(id);
  if (c) {
    snap->suspended = c->suspended;
    snap->header = c->header;
    snap->max_frame = c->max_frame;
    snap->count = c->tx_count;
    memcpy(snap->tx, c->tx, sizeof(snap->tx));
  }
  R_MUTEX_UNLOCK(sys_data_connector_mutex);
  return c != NULL;
}

/* Frame limit of one binding: the provider's current limit, capped by the connector's. */
static size_t binding_limit(const tx_snapshot_t* snap, const sys_data_connector_provider_t* ops, uint32_t endpoint, uint32_t peer) {
  size_t limit = snap->max_frame;
  if (ops->max_frame) {
    size_t provider_limit = ops->max_frame(endpoint, peer);
    if (provider_limit < limit) limit = provider_limit;
  }
  return limit;
}

size_t sys_data_connector_max_payload(uint8_t id) {
  tx_snapshot_t snap;
  if (!take_tx_snapshot(id, &snap)) return 0;
  size_t limit = snap.max_frame;
  for (uint8_t i = 0; i < snap.count; i++) {
    const sys_data_connector_provider_t* ops = find_provider(snap.tx[i].provider_id);
    if (!ops) continue;
    size_t binding = binding_limit(&snap, ops, snap.tx[i].endpoint, SYS_DATA_CONNECTOR_PEER_ALL);
    if (binding < limit) limit = binding;
  }
  return limit > 1 ? limit - 1 : 0;
}

#undef OWNER
#define OWNER OWNER_SYS_DATA_CONNECTOR_SEND
static SE_MUST_USE err_h send_frame(uint8_t id, const tx_snapshot_t* snap, const binding_t* b, uint32_t peer, const uint8_t* frame, size_t len) {
  const sys_data_connector_provider_t* ops = find_provider(b->provider_id);
  if (!ops) SE_FAIL(ERR_DATA_CONNECTOR_NO_PROVIDER, .provider_id = b->provider_id);
  size_t limit = binding_limit(snap, ops, b->endpoint, peer);
  if (len > limit) {
    SE_FAIL(ERR_DATA_CONNECTOR_FRAME_TOO_LONG, .id = id, .provider_id = b->provider_id, .len = (uint32_t)len, .max = (uint32_t)limit);
  }
  SE_TRY(ops->send(b->endpoint, peer, frame, len));
  return NULL;
}

/* Prepend the stream byte. Checks against the connector's own cap; each
   binding's (smaller) transport limit is checked in send_frame(). */
static SE_MUST_USE err_h build_frame(uint8_t id, const tx_snapshot_t* snap, const void* data, size_t len, uint8_t* frame, size_t* frame_len) {
  if (len + 1 > snap->max_frame) {
    SE_FAIL(ERR_DATA_CONNECTOR_FRAME_TOO_LONG, .id = id, .provider_id = 0, .len = (uint32_t)(len + 1), .max = snap->max_frame);
  }
  frame[0] = snap->header;
  memcpy(&frame[1], data, len);
  *frame_len = len + 1;
  return NULL;
}

err_h sys_data_connector_send(uint8_t id, const void* data, size_t len) {
  SE_CHECK_NOT_NULL(data);
  if (len == 0) return NULL;
  tx_snapshot_t snap;
  if (!take_tx_snapshot(id, &snap)) SE_FAIL(ERR_BASE_NOT_FOUND, id);
  if (snap.suspended || snap.count == 0) return NULL;

  uint8_t frame[CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX];
  size_t frame_len = 0;
  SE_TRY(build_frame(id, &snap, data, len, frame, &frame_len));

  /* Every binding gets the frame even if one fails; the first error is returned. */
  err_h first = NULL;
  for (uint8_t i = 0; i < snap.count; i++) {
    err_h err = send_frame(id, &snap, &snap.tx[i], SYS_DATA_CONNECTOR_PEER_ALL, frame, frame_len);
    if (err && !first) first = err;
    else SE_release(err);
  }
  return first;
}

err_h sys_data_connector_send_to(uint8_t id, const sys_data_connector_origin_t* to, const void* data, size_t len) {
  SE_CHECK_NOT_NULL(to);
  SE_CHECK_NOT_NULL(data);
  if (len == 0) return NULL;
  tx_snapshot_t snap;
  if (!take_tx_snapshot(id, &snap)) SE_FAIL(ERR_BASE_NOT_FOUND, id);
  if (snap.suspended) return NULL;
  const binding_t* b = find_binding(snap.tx, snap.count, to->provider_id);
  if (!b) return NULL;

  uint8_t frame[CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX];
  size_t frame_len = 0;
  SE_TRY(build_frame(id, &snap, data, len, frame, &frame_len));
  SE_TRY(send_frame(id, &snap, b, to->peer, frame, frame_len));
  return NULL;
}

// -----------------------------------------------------------------------------
// RX
// -----------------------------------------------------------------------------

#undef OWNER
#define OWNER OWNER_SYS_DATA_CONNECTOR_RECEIVE
err_h sys_data_connector_receive(uint8_t id, uint8_t* buf, size_t max_len, size_t* out_len, sys_data_connector_origin_t* out_origin) {
  SE_CHECK_NOT_NULL(buf);
  SE_CHECK_NOT_NULL(out_len);
  *out_len = 0;

  binding_t rx[CONFIG_SYS_DATA_CONNECTOR_PROVIDERS_MAX];
  R_MUTEX_LOCK(sys_data_connector_mutex, WAIT_FOREVER);
  connector_t* c = find_conn(id);
  if (!c) {
    R_MUTEX_UNLOCK(sys_data_connector_mutex);
    SE_FAIL(ERR_BASE_NOT_FOUND, id);
  }
  uint8_t count = c->suspended ? 0 : c->rx_count;
  uint8_t start = c->rx_next;
  memcpy(rx, c->rx, sizeof(rx));
  R_MUTEX_UNLOCK(sys_data_connector_mutex);

  for (uint8_t i = 0; i < count; i++) {
    uint8_t idx = (uint8_t)((start + i) % count);
    /* bind_rx only accepts providers with dequeue, and providers are never removed. */
    const sys_data_connector_provider_t* ops = find_provider(rx[idx].provider_id);
    uint32_t peer = 0;
    err_h err = ops->dequeue(rx[idx].endpoint, buf, max_len, out_len, &peer);
    if (!err && *out_len == 0) continue;

    R_MUTEX_LOCK(sys_data_connector_mutex, WAIT_FOREVER);
    c = find_conn(id);
    if (c && c->rx_count == count) c->rx_next = (uint8_t)((idx + 1) % count);
    R_MUTEX_UNLOCK(sys_data_connector_mutex);

    if (err) {
      *out_len = 0;
      SE_TRY(err);
    }
    if (out_origin) *out_origin = (sys_data_connector_origin_t){.provider_id = rx[idx].provider_id, .peer = peer};
    return NULL;
  }
  return NULL;
}

bool sys_data_connector_wait_rx(uint8_t id, uint32_t timeout_ms) {
  if (id >= CONFIG_SYS_DATA_CONNECTOR_MAX || !s_wake[id]) {
    vTaskDelay(pdMS_TO_TICKS(timeout_ms));
    return false;
  }
  return xSemaphoreTake(s_wake[id], pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void sys_data_connector_notify_rx(uint8_t id) {
  if (id < CONFIG_SYS_DATA_CONNECTOR_MAX && s_wake[id]) xSemaphoreGive(s_wake[id]);
}
