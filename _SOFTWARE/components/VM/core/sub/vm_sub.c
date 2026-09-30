#include "vm_sub.h"
#include "freertos/FreeRTOS.h"
#include "sys_data_connector.h"
#include "utils.h"
#include "vm_exec.h"
#include "vm_obj_access.h"
#include "vm_obj_dyn.h"
#include "vm_store.h"
#include "vm_wire.h"

// This file's DBG() calls fire on CONFIG_DBG_GLOBAL or this component's own
// switch (components/utils/Kconfig) - see DBG()'s doc comment in utils.h.
#define DBG_ENABLE CONFIG_DBG_ENABLE_VM

#define OWNER OWNER_VM_SUB
#define TAG "vm_sub"

/* Deepest PTR nesting walked below a subscribed object. */
#define SUB_MAX_DEPTH 8u

/* Frame header (class, packet, record count). */
#define SUB_FRAME_HDR_LEN 3u
/* Value record head (vm_wire_data_t) and describe record without its name
   (vm_wire_obj_t): the 0x43 / 0x42 upload layouts. */
#define SUB_VALUE_HDR_LEN sizeof(vm_wire_data_t)
#define SUB_DESC_HDR_LEN sizeof(vm_wire_obj_t)

/* ---------------------------------------------------------------------------
 * Subscription list
 *
 * The list is written by the interface task and read by the VM task. A new
 * list is staged under s_mux and taken over by the VM task at the start of its
 * next sample (or at once, under the program barrier, while the VM is
 * stopped), so the scan never sees a half-written list.
 * ------------------------------------------------------------------------- */

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static uint16_t s_pending_ids[CONFIG_VM_SUB_MAX_SUBSCRIBERS];
static uint16_t s_pending_count;
static bool s_pending;

static uint16_t s_subscribed_ids[CONFIG_VM_SUB_MAX_SUBSCRIBERS];
static uint16_t s_sub_count;

/* ---------------------------------------------------------------------------
 * Tracking table
 *
 * One entry per object reachable from the subscriptions: what was last sent.
 * Keyed by handle, so a pass needs no ID lookups (an arena ID is a registry
 * scan); the ID is looked up once, when the entry is made. A heap object's
 * handle can be freed and reused by another heap object, so its cached ID is
 * re-checked against the heap register (O(1)) every pass.
 *
 * An entry records what was *handed to the transport*. A record the
 * transport refused (TX buffer full) or couldn't carry (link too small) marks
 * its object SUB_LOST, and the object is sent in full again at the next
 * sample -- otherwise the app would keep a stale value until the next change.
 *
 * Open addressing, linear probing, sized for a load of at most 2/3. Entries
 * not reached in a pass are removed at its end.
 * ------------------------------------------------------------------------- */

#define SUB_SLOTS (CONFIG_VM_SUB_MAX_TRACKED + CONFIG_VM_SUB_MAX_TRACKED / 2u)
/* track_t.head: 14-bit header hash | flags */
#define SUB_HEAD_HASH 0x3FFFu
#define SUB_LOST 0x4000u /* last record didn't reach the transport: resend in full */
#define SUB_SEEN 0x8000u /* reached in this pass */

typedef struct {
  vm_obj_h obj;    /* NULL: free slot */
  uint32_t value;  /* hash of the value last sent */
  uint16_t id;     /* wire ID (VM_OBJ_ID_DYN_BIT set for heap objects) */
  uint16_t head;   /* hash of the header last described | SUB_LOST | SUB_SEEN */
} track_t;

static track_t s_track[SUB_SLOTS];
static uint16_t s_track_count;
static bool s_track_full_reported;

//#vm-telemetry-stream CONFIG_TX_PACKET_CLASS_TELEMETRY @class CONFIG_TX_PACKET_CLASS_VM_LOADER @description Device to app, on the telemetry stream: [stream][class][packet][u8 count][records]. Frames follow the link's frame size (BLE MTU), capped by CONFIG_VM_SUB_MAX_FRAME_LEN. A lost frame is resent in full the next pass; drop a value for a heap ID that has no describe.
//#vm-telemetry CONFIG_TX_PACKET_HEADER_VM_DESCRIBE @record vm_wire_obj_t @title Describe @description Heap objects (ID has VM_OBJ_ID_DYN_BIT), which the app never uploaded: type, size and name. Sent when first seen and when any of them change, always before a value that refers to them.
//#vm-telemetry CONFIG_TX_PACKET_HEADER_VM_SET_DATA @record vm_wire_data_t @title Values @description Subscribed objects and their PTR children (8 levels) whose value changed since last sent, quiet writes included; everything once after a subscribe. An object bigger than a frame is split over several records (start_idx).

/* ---------------------------------------------------------------------------
 * Frames
 *
 * Two frames are filled side by side: describe records (0x42 layout, heap
 * objects) and value records (0x43 layout). A value frame goes out only after
 * every describe queued before it. Once the transport refuses a frame, the
 * rest of the sample is held back: the pending values (they may reference a
 * lost describe) and everything not yet queued are marked lost and resent next
 * sample. So the app never gets a value for an object it can't interpret, and
 * a congested link isn't pushed further. Static: only one sample runs at a
 * time (the VM task, or the interface task under the program barrier).
 * ------------------------------------------------------------------------- */

typedef struct {
  uint8_t buf[CONFIG_VM_SUB_MAX_FRAME_LEN];
  size_t len;
  size_t cap; /* this frame's limit: the buffer, or less when the transport carries less */
  uint8_t count;
  uint8_t packet;
} sub_frame_t;

static sub_frame_t s_desc;
static sub_frame_t s_value;
static bool s_refused; /* the transport refused a frame in this sample */

static inline void put_u16(uint8_t* p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)(v >> 8);
}

static void frame_init(sub_frame_t* f, uint8_t packet) {
  f->packet = packet;
  f->buf[0] = CONFIG_TX_PACKET_CLASS_VM_LOADER;
  f->buf[1] = packet;
  f->buf[2] = 0;
  f->len = SUB_FRAME_HDR_LEN;
  f->count = 0;
  /* Re-read per frame: the telemetry transport's limit follows the link (BLE MTU). */
  size_t transport = sys_data_connector_max_payload(SYS_DATA_CONNECTOR_TELEMETRY);
  f->cap = transport < sizeof(f->buf) ? transport : sizeof(f->buf);
}

static void frame_lost(const sub_frame_t* f);

static void frame_send(sub_frame_t* f) {
  if (f->count == 0) return;
  f->buf[2] = f->count;
  DBG(ESP_LOGI(TAG, "TX 0x%02X: %u records (%u bytes)", (unsigned)f->packet, (unsigned)f->count, (unsigned)f->len););
  err_h err = sys_data_connector_send(SYS_DATA_CONNECTOR_TELEMETRY, f->buf, f->len);
  if (err) {
    s_refused = true;
    frame_lost(f);
    SE_push_to_handler(err);
    if (f == &s_desc) { /* pending values may reference what was just lost */
      frame_lost(&s_value);
      frame_init(&s_value, s_value.packet);
    }
  }
  frame_init(f, f->packet);
}

/* Send describes, then the values that may reference them. */
static void flush_values(void) {
  frame_send(&s_desc);
  frame_send(&s_value);
}

/* Room for `need` more bytes and one more record in f. */
static inline bool frame_fits(const sub_frame_t* f, size_t need) {
  return f->count < UINT8_MAX && f->len + need <= f->cap;
}

/* ---------------------------------------------------------------------------
 * Hashes (FNV-1a)
 * ------------------------------------------------------------------------- */

#define FNV_INIT 2166136261u

static inline uint32_t fnv(uint32_t h, const uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; i++) {
    h = (h ^ p[i]) * 16777619u;
  }
  return h;
}

/* Header as the app sees it: type, size, name. Flags other than the name are
   not part of it (upd changes every pass). */
static uint16_t head_hash(vm_obj_h o) {
  uint8_t shape[4] = {(uint8_t)o->head.payload_size, (uint8_t)(o->head.payload_size >> 8), o->head.d.obj_t,
                      o->head.f.tagged ? o->head.d.name_size : 0};
  uint32_t h = fnv(FNV_INIT, shape, sizeof(shape));
  if (shape[3]) h = fnv(h, o->payload + o->head.payload_size, shape[3]);
  return (uint16_t)((h ^ (h >> 16)) & SUB_HEAD_HASH);
}

/* ---------------------------------------------------------------------------
 * Tracking table operations
 * ------------------------------------------------------------------------- */

static inline uint32_t slot_of(vm_obj_h o) {
  uint32_t k = (uint32_t)(uintptr_t)o >> 2;
  return (k * 2654435761u) % SUB_SLOTS;
}

static void track_clear(void) {
  memset(s_track, 0, sizeof(s_track));
  s_track_count = 0;
  s_track_full_reported = false;
}

/* The entry for o, created if missing. *is_new tells the caller nothing has
   been sent for it yet. NULL when the table is full. */
static track_t* track_get(vm_obj_h o, bool* is_new) {
  uint32_t i = slot_of(o);
  while (s_track[i].obj) {
    if (s_track[i].obj == o) {
      track_t* t = &s_track[i];
      /* A heap handle can be freed and handed to another heap object. */
      if ((t->id & VM_OBJ_ID_DYN_BIT) && g_vm_dyn[t->id & (uint16_t)~VM_OBJ_ID_DYN_BIT].obj != o) {
        t->id = vm_obj_get_id(o);
        *is_new = true;
      } else {
        *is_new = false;
      }
      return t;
    }
    i = (i + 1u) % SUB_SLOTS;
  }

  if (s_track_count >= CONFIG_VM_SUB_MAX_TRACKED) {
    if (!s_track_full_reported) {
      s_track_full_reported = true;
      SE_RAISE(ERR_VM_SUB_TRACK_FULL, .max = CONFIG_VM_SUB_MAX_TRACKED);
    }
    return NULL;
  }
  s_track[i] = (track_t){.obj = o, .value = 0, .id = vm_obj_get_id(o), .head = 0};
  s_track_count++;
  *is_new = true;
  return &s_track[i];
}

/* Wire ID of an object, from its entry when it has one. */
static uint16_t wire_id(vm_obj_h o) {
  if (!o) return VM_OBJ_ID_NONE;
  uint32_t i = slot_of(o);
  while (s_track[i].obj) {
    if (s_track[i].obj == o) return s_track[i].id;
    i = (i + 1u) % SUB_SLOTS;
  }
  return vm_obj_get_id(o);
}

/* Rare path (a refused frame): a scan of the table per record. */
static void track_mark_lost(uint16_t id) {
  for (uint32_t i = 0; i < SUB_SLOTS; i++) {
    if (s_track[i].obj && s_track[i].id == id) s_track[i].head |= SUB_LOST;
  }
}

/* Every object with a record in a frame the transport refused. */
static void frame_lost(const sub_frame_t* f) {
  size_t off = SUB_FRAME_HDR_LEN;
  for (uint8_t r = 0; r < f->count; r++) {
    if (f->packet == CONFIG_TX_PACKET_HEADER_VM_DESCRIBE) {
      vm_wire_obj_t rec;
      vm_obj_head_t head;
      memcpy(&rec, f->buf + off, sizeof(rec));
      memcpy(&head, rec.head, VM_OBJ_HEAD_WIRE_SIZE);
      track_mark_lost(rec.id);
      off += sizeof(rec) + head.d.name_size;
    } else {
      vm_wire_data_t rec;
      memcpy(&rec, f->buf + off, sizeof(rec));
      track_mark_lost(rec.id);
      off += sizeof(rec) + rec.byte_len;
    }
  }
}

/* Drop entries not reached in this pass; clear the seen mark on the rest.
   Backward-shift deletion keeps every probe chain intact without tombstones. */
static void track_sweep(void) {
  for (uint32_t i = 0; i < SUB_SLOTS;) {
    if (!s_track[i].obj) {
      i++;
      continue;
    }
    if (s_track[i].head & SUB_SEEN) {
      s_track[i].head &= (uint16_t)~SUB_SEEN;
      i++;
      continue;
    }
    uint32_t hole = i;
    uint32_t j = i;
    for (;;) {
      j = (j + 1u) % SUB_SLOTS;
      if (!s_track[j].obj) break;
      uint32_t home = slot_of(s_track[j].obj);
      /* Entry j may move into the hole if its home is not in (hole, j]. */
      bool stays = (hole <= j) ? (hole < home && home <= j) : (hole < home || home <= j);
      if (!stays) {
        s_track[hole] = s_track[j];
        hole = j;
      }
    }
    s_track[hole].obj = NULL;
    s_track_count--;
    /* Slot i now holds a shifted entry (or is free): look at it again. */
  }
}

/* ---------------------------------------------------------------------------
 * Records
 * ------------------------------------------------------------------------- */

/* Describe record: vm_wire_obj_t, then the name. Same layout as a 0x42 upload
   record, so the app parses both with one decoder. */
static void emit_describe(track_t* t, vm_obj_h o) {
  uint16_t id = t->id;
  uint8_t name_len = o->head.f.tagged ? o->head.d.name_size : 0;
  size_t need = SUB_DESC_HDR_LEN + name_len;
  if (!frame_fits(&s_desc, need)) frame_send(&s_desc);
  if (s_refused || !frame_fits(&s_desc, need)) { /* held back, or the link can't carry it */
    t->head |= SUB_LOST;
    return;
  }

  vm_obj_head_t head = o->head;
  head.d.name_size = name_len;
  vm_wire_obj_t rec = {.id = id};
  memcpy(rec.head, &head, VM_OBJ_HEAD_WIRE_SIZE);
  uint8_t* p = s_desc.buf + s_desc.len;
  memcpy(p, &rec, sizeof(rec));
  if (name_len) memcpy(p + SUB_DESC_HDR_LEN, o->payload + o->head.payload_size, name_len);
  s_desc.len += need;
  s_desc.count++;
}

/* Value records: vm_wire_data_t, then the data. An object larger than a frame
   goes out in several records; start_idx counts elements (a PTR element is its
   child's u16 ID). */
static void emit_value(track_t* t, vm_obj_h o) {
  uint16_t id = t->id;
  bool is_ptr = (vm_obj_t_e)o->head.d.obj_t == VM_OBJ_PTR;
  uint16_t items = vm_obj_get_items_cnt(o);
  size_t elem = is_ptr ? VM_OBJ_PTR_WIRE_SIZE : vm_obj_get_type_size(o);
  if (elem == 0) return;

  uint16_t start = 0;
  do {
    if (!frame_fits(&s_value, SUB_VALUE_HDR_LEN + (items ? elem : 0))) flush_values();
    if (s_refused || !frame_fits(&s_value, SUB_VALUE_HDR_LEN + (items ? elem : 0))) { /* held back, or the link can't carry one element */
      t->head |= SUB_LOST;
      return;
    }

    size_t room = (s_value.cap - s_value.len - SUB_VALUE_HDR_LEN) / elem;
    uint16_t n = (uint16_t)((items - start) < room ? (items - start) : room);
    uint16_t byte_len = (uint16_t)(n * elem);

    vm_wire_data_t rec = {.id = id, .start_idx = start, .byte_len = byte_len};
    uint8_t* p = s_value.buf + s_value.len;
    memcpy(p, &rec, sizeof(rec));
    if (is_ptr) {
      vm_obj_h* children = (vm_obj_h*)o->payload;
      for (uint16_t k = 0; k < n; k++) {
        put_u16(p + SUB_VALUE_HDR_LEN + k * VM_OBJ_PTR_WIRE_SIZE, wire_id(children[start + k]));
      }
    } else if (byte_len) {
      memcpy(p + SUB_VALUE_HDR_LEN, o->payload + (size_t)start * elem, byte_len);
    }
    s_value.len += SUB_VALUE_HDR_LEN + byte_len;
    s_value.count++;
    start = (uint16_t)(start + n);
  } while (start < items);
}

/* ---------------------------------------------------------------------------
 * Walk
 *
 * Post-order: children are tracked (and their IDs known) before their parent's
 * value is hashed. A PTR's value is its list of child IDs, so a child swapped
 * for another one resends the parent. Every object is visited once per pass
 * however many subscriptions reach it.
 * ------------------------------------------------------------------------- */

static void walk(vm_obj_h o, uint8_t depth) {
  if (!o || depth > SUB_MAX_DEPTH) return;

  bool is_new;
  track_t* t = track_get(o, &is_new);
  if (!t || t->id == VM_OBJ_ID_NONE) return;
  if (t->head & SUB_SEEN) return;
  t->head |= SUB_SEEN;

  bool is_ptr = (vm_obj_t_e)o->head.d.obj_t == VM_OBJ_PTR;
  uint16_t items = vm_obj_get_items_cnt(o);
  if (is_ptr) {
    vm_obj_h* children = (vm_obj_h*)o->payload;
    /* Inserting never moves an entry (only the end-of-pass sweep does), so t
       stays valid while the children are added. */
    for (uint16_t i = 0; i < items; i++) walk(children[i], (uint8_t)(depth + 1u));
  }

  uint32_t value = FNV_INIT;
  if (is_ptr) {
    vm_obj_h* children = (vm_obj_h*)o->payload;
    for (uint16_t i = 0; i < items; i++) {
      uint16_t cid = wire_id(children[i]);
      value = fnv(value, (const uint8_t*)&cid, sizeof(cid));
    }
  } else {
    value = fnv(value, o->payload, o->head.payload_size);
  }

  bool resend = is_new || (t->head & SUB_LOST);
  t->head &= (uint16_t)~SUB_LOST;

  /* Heap objects are unknown to the app until described; program objects it
     uploaded itself. */
  if (t->id & VM_OBJ_ID_DYN_BIT) {
    uint16_t head = head_hash(o);
    if (resend || head != (t->head & SUB_HEAD_HASH)) {
      t->head = (uint16_t)(head | SUB_SEEN);
      emit_describe(t, o);
      if (t->head & SUB_LOST) return; /* no value without its describe */
      resend = true;                  /* a re-described object is sent in full */
    }
  }

  if (resend || value != t->value) {
    t->value = value;
    emit_value(t, o);
  }
}

/* Take over a list staged by vm_sub_subscribe(). Every object is sent in full
   at the next sample (the table starts empty). */
static void take_pending(void) {
  portENTER_CRITICAL(&s_mux);
  bool pending = s_pending;
  if (pending) {
    memcpy(s_subscribed_ids, s_pending_ids, (size_t)s_pending_count * sizeof(uint16_t));
    s_sub_count = s_pending_count;
    s_pending = false;
  }
  portEXIT_CRITICAL(&s_mux);
  if (pending) track_clear();
}

static void sample(void) {
  if (s_sub_count == 0) return;

  s_refused = false;
  frame_init(&s_desc, CONFIG_TX_PACKET_HEADER_VM_DESCRIBE);
  frame_init(&s_value, CONFIG_TX_PACKET_HEADER_VM_SET_DATA);
  for (uint16_t i = 0; i < s_sub_count; i++) {
    walk(vm_obj_lookup_by_id(s_subscribed_ids[i]), 0);
  }
  flush_values();
  track_sweep();
}

/* ---------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

err_h vm_sub_init(void) {
  vm_exec_set_sample_hook(vm_sub_scan);
  return NULL;
}

err_h vm_sub_subscribe(const uint16_t* ids, uint16_t count) {
  if (count > CONFIG_VM_SUB_MAX_SUBSCRIBERS) {
    SE_FAIL(ERR_INVALID_VAL_UI32, .val = count, .min = 0, .max = CONFIG_VM_SUB_MAX_SUBSCRIBERS);
  }
  if (count) SE_CHECK_NOT_NULL(ids);

  portENTER_CRITICAL(&s_mux);
  if (count) memcpy(s_pending_ids, ids, (size_t)count * sizeof(uint16_t));
  s_pending_count = count;
  s_pending = true;
  portEXIT_CRITICAL(&s_mux);
  DBG(ESP_LOGI(TAG, "subscribed to %u objects", (unsigned)count););

  /* A running program sends the snapshot after its next pass. A stopped one
     has no next pass, so it is sent now, under the program barrier (which is
     only taken while stopped: it would cancel a running or frozen pass). */
  if (vm_exec_mode() != VM_RUN_STOPPED) return NULL;
  vm_run_mode_e previous;
  SE_TRY(vm_exec_program_lock(&previous));
  vm_sub_scan();
  vm_exec_program_unlock(previous);
  return NULL;
}

err_h vm_sub_handle_packet(const uint8_t* body, size_t len) {
  SE_CHECK_NOT_NULL(body);
  if (len < 1) {
    SE_FAIL(ERR_VM_LOAD_SHORT_RECORD, .packet = CONFIG_RX_PACKET_HEADER_VM_SUBSCRIBE, .need = 1, .got = (uint16_t)len);
  }

  uint8_t count = body[0];
  if (count > CONFIG_VM_SUB_MAX_SUBSCRIBERS) {
    SE_FAIL(ERR_INVALID_VAL_UI32, .val = count, .min = 0, .max = CONFIG_VM_SUB_MAX_SUBSCRIBERS);
  }
  size_t need = 1u + (size_t)count * sizeof(vm_wire_sub_t);
  if (len < need) {
    SE_FAIL(ERR_VM_LOAD_SHORT_RECORD, .packet = CONFIG_RX_PACKET_HEADER_VM_SUBSCRIBE, .need = (uint16_t)need, .got = (uint16_t)len);
  }

  uint16_t ids[CONFIG_VM_SUB_MAX_SUBSCRIBERS];
  for (uint16_t i = 0; i < count; i++) {
    vm_wire_sub_t rec;
    memcpy(&rec, body + 1 + (size_t)i * sizeof(rec), sizeof(rec));
    ids[i] = rec.id;
  }
  return vm_sub_subscribe(ids, count);
}

void vm_sub_scan(void) {
  take_pending();
  sample();
}

void vm_sub_reset(void) {
  portENTER_CRITICAL(&s_mux);
  s_pending = false;
  s_pending_count = 0;
  s_sub_count = 0;
  portEXIT_CRITICAL(&s_mux);
  track_clear();
  DBG(ESP_LOGI(TAG, "subscriptions cleared"););
}

uint16_t vm_sub_count(void) {
  return s_sub_count;
}

const uint16_t* vm_sub_get_ids(uint16_t* out_count) {
  if (out_count) *out_count = s_sub_count;
  return s_subscribed_ids;
}
