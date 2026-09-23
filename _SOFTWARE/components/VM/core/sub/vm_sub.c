#include "vm_sub.h"
#include "sys_data_connector.h"
#include "utils.h"
#include "vm_exec.h"
#include "vm_obj_access.h"
#include "vm_obj_dyn.h"
#include "vm_store.h"

// This file's DBG() calls fire on CONFIG_DBG_GLOBAL or this component's own
// switch (components/utils/Kconfig) - see DBG()'s doc comment in utils.h.
#define DBG_ENABLE CONFIG_DBG_ENABLE_VM

#define OWNER OWNER_VM_BASE
#define TAG "vm_sub"

static uint16_t s_subscribed_ids[CONFIG_VM_SUB_MAX_SUBSCRIBERS];
static uint16_t s_sub_count = 0;

static uint16_t s_emitted[CONFIG_VM_SUB_MAX_EMITTED_PER_PASS];
static uint16_t s_emitted_count = 0;

/* Frame header (class, packet, record count) and per-record header (id, start, byte_len). */
#define SUB_FRAME_HDR_LEN 3u
#define SUB_RECORD_HDR_LEN 6u

typedef struct {
  uint8_t buf[CONFIG_VM_SUB_MAX_FRAME_LEN];
  size_t len;
  size_t cap; /* this frame's limit: the buffer, or less when the transport carries less */
  uint8_t count;
} sub_frame_t;

static uint16_t get_obj_id(vm_obj_h o) {
  if (!o) return VM_ID_NONE;
  if (vm_obj_is_dynamic(o)) {
    return vm_obj_dyn_get_id(o);
  }
  const vm_registry_t* g = &g_vm_store.reg[VM_REG_OBJ];
  for (uint16_t i = 0; i < g->count; i++) {
    if (g->items[i] == (void*)o) return i;
  }
  return VM_ID_NONE;
}

static bool is_already_emitted(uint16_t id) {
  for (uint16_t i = 0; i < s_emitted_count; i++) {
    if (s_emitted[i] == id) return true;
  }
  return false;
}

static void mark_emitted(uint16_t id) {
  if (s_emitted_count < CONFIG_VM_SUB_MAX_EMITTED_PER_PASS) {
    s_emitted[s_emitted_count++] = id;
  }
}

static void frame_init(sub_frame_t* f) {
  f->buf[0] = CONFIG_TX_PACKET_CLASS_VM_LOADER;
  f->buf[1] = CONFIG_TX_PACKET_HEADER_VM_SET_DATA;
  f->buf[2] = 0;
  f->len = SUB_FRAME_HDR_LEN;
  f->count = 0;
  /* Re-read per frame: the telemetry transport's limit follows the link (BLE MTU). */
  size_t transport = sys_data_connector_max_payload(SYS_DATA_CONNECTOR_TELEMETRY);
  f->cap = transport < sizeof(f->buf) ? transport : sizeof(f->buf);
}

static void frame_flush(sub_frame_t* f) {
  if (!f || f->count == 0) return;
  f->buf[2] = f->count;

  // Log telemetry packet details
  DBG(
    ESP_LOGI(TAG, "TX Telemetry: %u records (%u bytes)", (unsigned)f->count, (unsigned)f->len);
    size_t off = 3;
    for (uint8_t i = 0; i < f->count && (off + 6) <= f->len; i++) {
      uint16_t id = (uint16_t)(f->buf[off] | ((uint16_t)f->buf[off + 1] << 8));
      uint16_t start_idx = (uint16_t)(f->buf[off + 2] | ((uint16_t)f->buf[off + 3] << 8));
      uint16_t byte_len = (uint16_t)(f->buf[off + 4] | ((uint16_t)f->buf[off + 5] << 8));
      off += 6;
      if (off + byte_len <= f->len) {
        if (byte_len == 4) {
          float fval = 0.0f;
          memcpy(&fval, f->buf + off, 4);
          ESP_LOGI(TAG, "  [rec %u] OBJ %u (start=%u, len=4): float=%f", (unsigned)i, (unsigned)id, (unsigned)start_idx, (double)fval);
        } else if (byte_len == 1) {
          ESP_LOGI(TAG, "  [rec %u] OBJ %u (start=%u, len=1): val=%u", (unsigned)i, (unsigned)id, (unsigned)start_idx, (unsigned)f->buf[off]);
        } else if (byte_len == 2) {
          uint16_t u16val = (uint16_t)(f->buf[off] | ((uint16_t)f->buf[off + 1] << 8));
          ESP_LOGI(TAG, "  [rec %u] OBJ %u (start=%u, len=2): u16=%u", (unsigned)i, (unsigned)id, (unsigned)start_idx, (unsigned)u16val);
        } else {
          ESP_LOGI(TAG, "  [rec %u] OBJ %u (start=%u, len=%u B)", (unsigned)i, (unsigned)id, (unsigned)start_idx, (unsigned)byte_len);
        }
        off += byte_len;
      }
    }

    // Format and log raw hex buffer
    char hex_buf[96];
    size_t hex_len = 0;
    for (size_t i = 0; i < f->len && hex_len + 3 < sizeof(hex_buf); i++) {
      hex_len += (size_t)snprintf(hex_buf + hex_len, sizeof(hex_buf) - hex_len, "%02X ", f->buf[i]);
    }
    ESP_LOGI(TAG, "  Frame Hex: [ %s%s]", hex_buf, (f->len * 3 >= sizeof(hex_buf)) ? "..." : "");
  );

  SE_REPORT(sys_data_connector_send(SYS_DATA_CONNECTOR_TELEMETRY, f->buf, f->len));
  frame_init(f);
}

static void frame_append_obj(sub_frame_t* f, uint16_t id, vm_obj_h o) {
  if (!o) return;

  uint16_t byte_len = 0;
  bool is_ptr = ((vm_obj_t_e)o->head.d.obj_t == VM_OBJ_PTR);

  if (is_ptr) {
    uint16_t items = vm_obj_get_items_cnt(o);
    byte_len = (uint16_t)(items * 2u);
  } else {
    byte_len = o->head.payload_size;
  }

  // If adding this record exceeds maximum frame length, flush the current frame
  if (f->len + SUB_RECORD_HDR_LEN + byte_len > f->cap) {
    frame_flush(f);
  }

  /* The transport can't carry even an empty record (no client link yet
     reports less than SUB_FRAME_HDR_LEN + SUB_RECORD_HDR_LEN): skip it. */
  if (f->len + SUB_RECORD_HDR_LEN > f->cap) return;

  // If a single record alone is larger than remaining space in an empty frame, cap to frame limit
  if (f->len + SUB_RECORD_HDR_LEN + byte_len > f->cap) {
    byte_len = (uint16_t)(f->cap - f->len - SUB_RECORD_HDR_LEN);
  }

  uint8_t* p = f->buf + f->len;
  // u16 id
  p[0] = (uint8_t)(id & 0xFFu);
  p[1] = (uint8_t)((id >> 8) & 0xFFu);
  // u16 start_idx
  p[2] = 0;
  p[3] = 0;
  // u16 byte_len
  p[4] = (uint8_t)(byte_len & 0xFFu);
  p[5] = (uint8_t)((byte_len >> 8) & 0xFFu);

  if (is_ptr) {
    uint16_t items = vm_obj_get_items_cnt(o);
    vm_obj_h* children = (vm_obj_h*)o->payload;
    for (uint16_t i = 0; i < items && (i * 2u + 1u) < byte_len; i++) {
      uint16_t cid = get_obj_id(children[i]);
      p[6 + i * 2] = (uint8_t)(cid & 0xFFu);
      p[6 + i * 2 + 1] = (uint8_t)((cid >> 8) & 0xFFu);
    }
  } else {
    if (byte_len > 0) {
      memcpy(p + 6, o->payload, byte_len);
    }
  }

  f->len += 6 + byte_len;
  f->count++;
}

static bool tree_has_update(vm_obj_h o, int depth) {
  if (!o || depth > 8) return false;
  if (o->head.f.upd) return true;

  if ((vm_obj_t_e)o->head.d.obj_t == VM_OBJ_PTR) {
    uint16_t items = vm_obj_get_items_cnt(o);
    vm_obj_h* children = (vm_obj_h*)o->payload;
    for (uint16_t i = 0; i < items; i++) {
      if (tree_has_update(children[i], depth + 1)) return true;
    }
  }
  return false;
}

static void emit_tree(sub_frame_t* f, vm_obj_h o, int depth) {
  if (!o || depth > 8) return;

  uint16_t id = get_obj_id(o);
  if (id != VM_ID_NONE && !is_already_emitted(id)) {
    mark_emitted(id);
    frame_append_obj(f, id, o);
  }

  if ((vm_obj_t_e)o->head.d.obj_t == VM_OBJ_PTR) {
    uint16_t items = vm_obj_get_items_cnt(o);
    vm_obj_h* children = (vm_obj_h*)o->payload;
    for (uint16_t i = 0; i < items; i++) {
      emit_tree(f, children[i], depth + 1);
    }
  }
}

err_h vm_sub_init(void) {
  vm_exec_set_sample_hook(vm_sub_scan);
  return NULL;
}

err_h vm_sub_subscribe(const uint16_t* ids, uint16_t count) {
  if (count > CONFIG_VM_SUB_MAX_SUBSCRIBERS) {
    SE_FAIL(ERR_INVALID_VAL_UI32, .val = count, .min = 0, .max = CONFIG_VM_SUB_MAX_SUBSCRIBERS);
  }

  s_sub_count = 0;
  for (uint16_t i = 0; i < count; i++) {
    s_subscribed_ids[s_sub_count++] = ids[i];
    DBG(ESP_LOGI(TAG, "  -> subscribed obj_id=%u", (unsigned)ids[i]););
  }

  DBG(ESP_LOGI(TAG, "subscribed to %u objects total", (unsigned)s_sub_count););
  return NULL;
}

err_h vm_sub_handle_packet(const uint8_t* body, size_t len) {
  SE_CHECK_NOT_NULL(body);
  if (len < 1) {
    SE_FAIL(ERR_VM_LOAD_SHORT_RECORD, .packet = CONFIG_RX_PACKET_HEADER_VM_SUBSCRIBE, .need = 1, .got = (uint16_t)len);
  }

  uint8_t count = body[0];
  size_t need = 1u + (size_t)count * 2u;
  if (count > 0 && len < need) {
    SE_FAIL(ERR_VM_LOAD_SHORT_RECORD, .packet = CONFIG_RX_PACKET_HEADER_VM_SUBSCRIBE, .need = (uint16_t)need, .got = (uint16_t)len);
  }

  if (count == 0) {
    vm_sub_reset();
    return NULL;
  }

  uint16_t ids[CONFIG_VM_SUB_MAX_SUBSCRIBERS];
  uint16_t actual_cnt = count;
  if (actual_cnt > CONFIG_VM_SUB_MAX_SUBSCRIBERS) {
    actual_cnt = CONFIG_VM_SUB_MAX_SUBSCRIBERS;
  }

  for (uint16_t i = 0; i < actual_cnt; i++) {
    ids[i] = (uint16_t)(body[1 + i * 2] | ((uint16_t)body[1 + i * 2 + 1] << 8));
  }

  return vm_sub_subscribe(ids, actual_cnt);
}

void vm_sub_scan(void) {
  if (s_sub_count == 0) return;

  s_emitted_count = 0;
  sub_frame_t frame;
  frame_init(&frame);

  for (uint16_t i = 0; i < s_sub_count; i++) {
    uint16_t id = s_subscribed_ids[i];
    vm_obj_h o = vm_obj_get_by_id(id);
    if (!o) continue;

    if (tree_has_update(o, 0)) {
      emit_tree(&frame, o, 0);
    }
  }

  frame_flush(&frame);
}

void vm_sub_reset(void) {
  s_sub_count = 0;
  s_emitted_count = 0;
  DBG(ESP_LOGI(TAG, "subscriptions cleared"););
}

uint16_t vm_sub_count(void) {
  return s_sub_count;
}

const uint16_t* vm_sub_get_ids(uint16_t* out_count) {
  if (out_count) *out_count = s_sub_count;
  return s_subscribed_ids;
}
