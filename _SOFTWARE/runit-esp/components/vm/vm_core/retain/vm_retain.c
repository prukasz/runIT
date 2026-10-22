#include "vm_retain.h"
#include "vm_err.h"

#include "nvs.h"
#include "nvs_flash.h"
#include "utils.h"
#include "vm_obj_access.h"
#include "vm_store.h"

#define VM_RETAIN_NS     "vm"          // NVS namespace (default partition)
#define VM_RETAIN_KEY    "retain"     // the one blob in it
#define RETAIN_HDR_LEN   2u          // u16 count
#define RETAIN_REC_FIXED 4u        // u8 name_len + u8 type + u16 size
#define RETAIN_PERIOD_MS ((uint64_t)CONFIG_VM_RETAIN_PERIOD_S * 1000u)

U_TASK_DEFINE(vm_retain_task_h, CONFIG_VM_RETAIN_TASK_STACK);

/* The retained snapshot is one NVS blob. The default partition is initialised by the BLE stack as well; a second
   nvs_flash_init is a no-op that returns ESP_OK. */
static struct {
  nvs_handle_t nvs;
  bool nvs_ready;

  /* Capture buffer: written by the VM task at a pass end, or by a control task
     while the VM is quiescent -- never both at once. */
  uint8_t stage[CONFIG_VM_RETAIN_MAX_BYTES];
  size_t stage_len;

  /* Last snapshot handed to the retain task (under vm_retain_mutex). */
  uint8_t last[CONFIG_VM_RETAIN_MAX_BYTES];
  size_t last_len;
  bool dirty;  // last not written yet

  uint32_t budget;               // record bytes the loaded program reserved
  volatile bool restore_pending;  // loaded, not started yet
  volatile bool active;           // started, not torn down or cleared: saving allowed
  uint64_t next_ms;
  bool retry;  // hand-over failed (retain task busy): try on the next pass
} s_retain;

static err_h retain_nvs_open(void) {
  if (s_retain.nvs_ready) return NULL;
  esp_err_t rc = nvs_flash_init();
  if (rc == ESP_ERR_NVS_NO_FREE_PAGES || rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ERR_TRY_ESP(nvs_flash_erase());
    rc = nvs_flash_init();
  }
  ERR_TRY_ESP(rc);
  ERR_TRY_ESP(nvs_open(VM_RETAIN_NS, NVS_READWRITE, &s_retain.nvs));
  s_retain.nvs_ready = true;
  return NULL;
}

static err_h retain_store(const void* data, size_t len) {
  ERR_TRY(retain_nvs_open());
  ERR_TRY_ESP(nvs_set_blob(s_retain.nvs, VM_RETAIN_KEY, data, len));
  return ERR_ESP(nvs_commit(s_retain.nvs));
}

// `found` false when nothing is stored (not an error).
static err_h retain_load(void* buf, size_t cap, size_t* len, bool* found) {
  *found = false;
  ERR_TRY(retain_nvs_open());
  size_t stored = 0;
  esp_err_t rc = nvs_get_blob(s_retain.nvs, VM_RETAIN_KEY, NULL, &stored);
  if (rc == ESP_ERR_NVS_NOT_FOUND) return NULL;
  ERR_TRY_ESP(rc);
  ERR_CHECK(stored <= cap, ERR_VM_RETAIN_CORRUPT, .offset = (uint32_t)stored);
  ERR_TRY_ESP(nvs_get_blob(s_retain.nvs, VM_RETAIN_KEY, buf, &stored));
  *len = stored;
  *found = true;
  return NULL;
}

static err_h retain_erase(void) {
  ERR_TRY(retain_nvs_open());
  esp_err_t rc = nvs_erase_key(s_retain.nvs, VM_RETAIN_KEY);
  if (rc != ESP_ERR_NVS_NOT_FOUND) ERR_TRY_ESP(rc);
  return ERR_ESP(nvs_commit(s_retain.nvs));
}

/* Guards s_retain.last / s_retain.last_len / s_retain.dirty and the s_retain.active change on clear. The
   VM task only ever try-locks it, so a flash write in progress never stalls a
   pass: the capture is handed over on a later pass instead. */
U_MUTEX_DEFINE(vm_retain_mutex);

/* Serialize every retentive object; the load-time budget guarantees it fits. */
static size_t capture(uint8_t* buf) {
  const vm_registry_t* g = &g_vm_store.reg[VM_REG_OBJ];
  size_t off = RETAIN_HDR_LEN;
  uint16_t count = 0;
  for (uint16_t i = 0; i < g->count; i++) {
    vm_obj_h o = (vm_obj_h)g->items[i];
    if (!o || !o->head.f.retentive) continue;
    uint8_t name_len = 0;
    const char* name = vm_obj_get_tag(o, &name_len);
    const uint16_t size = o->head.payload_size;
    if (off + RETAIN_REC_FIXED + name_len + size > CONFIG_VM_RETAIN_MAX_BYTES) break;
    buf[off++] = name_len;
    memcpy(&buf[off], name, name_len);
    off += name_len;
    buf[off++] = o->head.d.obj_t;
    memcpy(&buf[off], &size, sizeof(size));
    off += sizeof(size);
    memcpy(&buf[off], o->payload, size);
    off += size;
    count++;
  }
  memcpy(buf, &count, sizeof(count));
  return off;
}

/* Give the capture to the retain task if it differs from the last one. False
   when the lock was busy (VM task: try again next pass). */
static bool hand_over(bool wait) {
  if (!U_MUTEX_LOCK(vm_retain_mutex, wait ? WAIT_FOREVER : 0)) return false;
  bool changed = false;
  if (s_retain.active) {
    changed = s_retain.stage_len != s_retain.last_len || memcmp(s_retain.stage, s_retain.last, s_retain.stage_len) != 0;
    if (changed) {
      memcpy(s_retain.last, s_retain.stage, s_retain.stage_len);
      s_retain.last_len = s_retain.stage_len;
      s_retain.dirty = true;
    }
  }
  U_MUTEX_UNLOCK(vm_retain_mutex);
  if (changed && vm_retain_task_h) xTaskNotifyGive(vm_retain_task_h);
  return true;
}

static vm_obj_h find_retentive(const char* name, uint8_t name_len, uint16_t* out_id) {
  const vm_registry_t* g = &g_vm_store.reg[VM_REG_OBJ];
  for (uint16_t i = 0; i < g->count; i++) {
    vm_obj_h o = (vm_obj_h)g->items[i];
    if (!o || !o->head.f.retentive) continue;
    uint8_t len = 0;
    const char* tag = vm_obj_get_tag(o, &len);
    if (len == name_len && memcmp(tag, name, len) == 0) {
      *out_id = i;
      return o;
    }
  }
  return NULL;
}

/* Put stored values into matching objects. The record is read from flash, so
   every length is checked; a malformed record stops at the bad byte. */
static MUST_USE err_h apply(const uint8_t* buf, size_t len) {
  if (len < RETAIN_HDR_LEN) return ERR_NEW(ERR_VM_RETAIN_CORRUPT, .offset = 0);
  uint16_t count;
  memcpy(&count, buf, sizeof(count));
  size_t off = RETAIN_HDR_LEN;
  for (uint16_t i = 0; i < count; i++) {
    if (off + 1 > len) return ERR_NEW(ERR_VM_RETAIN_CORRUPT, .offset = (uint32_t)off);
    const uint8_t name_len = buf[off++];
    if (name_len == 0 || name_len > VM_OBJ_NAME_MAX || off + name_len + 3u > len) {
      return ERR_NEW(ERR_VM_RETAIN_CORRUPT, .offset = (uint32_t)off);
    }
    const char* name = (const char*)&buf[off];
    off += name_len;
    const uint8_t type = buf[off++];
    uint16_t size;
    memcpy(&size, &buf[off], sizeof(size));
    off += sizeof(size);
    if (off + size > len) return ERR_NEW(ERR_VM_RETAIN_CORRUPT, .offset = (uint32_t)off);

    uint16_t id = 0;
    vm_obj_h o = find_retentive(name, name_len, &id);
    if (o) {
      if (o->head.d.obj_t != type || o->head.payload_size != size) {
        /* Renamed-in-place or retyped: keep the uploaded value. */
        VM_RAISE(ERR_VM_RETAIN_MISMATCH, .obj_id = id, .type = type, .size = size);
      } else {
        memcpy(o->payload, &buf[off], size);
      }
    }
    off += size;
  }
  return NULL;
}

static void vm_retain_task(void* arg) {
  (void)arg;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    U_MUTEX_LOCK(vm_retain_mutex, WAIT_FOREVER);
    err_h err = NULL;
    if (s_retain.dirty) {
      err = retain_store(s_retain.last, s_retain.last_len);
      if (!err) s_retain.dirty = false;  // on failure it stays dirty and goes out with the next change
    }
    U_MUTEX_UNLOCK(vm_retain_mutex);
    err_report(err);
  }
}

err_h vm_retain_init(void) {
  if (vm_retain_task_h == NULL) {
    U_TASK_START(vm_retain_task_h, vm_retain_task, NULL, CONFIG_VM_RETAIN_TASK_PRIO);
    if (vm_retain_task_h == NULL) return ERR_NEW(ERR_VM_NO_MEM);
  }
  return retain_nvs_open();
}

void vm_retain_on_open(void) {
  s_retain.budget = 0;
  s_retain.active = false;
  s_retain.restore_pending = true;
}

void vm_retain_on_reset(void) {
  s_retain.budget = 0;
  s_retain.active = false;
  s_retain.restore_pending = false;
}

err_h vm_retain_reserve(uint16_t obj_id, const vm_obj_head_t* head) {
  ERR_CHECK(head != NULL, ERR_VM_BAD_ARG);
  if (!head->f.retentive) return NULL;
  if (head->d.name_size == 0) return ERR_NEW(ERR_VM_RETAIN_UNNAMED, .obj_id = obj_id);
  const uint32_t used = s_retain.budget ? s_retain.budget : RETAIN_HDR_LEN;
  const uint32_t need = used + RETAIN_REC_FIXED + head->d.name_size + head->payload_size;
  if (need > CONFIG_VM_RETAIN_MAX_BYTES) {
    return ERR_NEW(ERR_VM_RETAIN_TOO_BIG, .obj_id = obj_id, .need = need, .max = CONFIG_VM_RETAIN_MAX_BYTES);
  }
  s_retain.budget = need;
  return NULL;
}

err_h vm_retain_restore(void) {
  if (!s_retain.restore_pending) return NULL;
  s_retain.restore_pending = false;

  size_t len = 0;
  bool found = false;
  err_h err = retain_load(s_retain.stage, sizeof(s_retain.stage), &len, &found);
  if (!err && found) err = apply(s_retain.stage, len);

  /* What the objects hold now is what is stored: no rewrite on the first
     period unless something changes. Saving starts even if the restore
     failed, so the program's values are kept from here on. */
  s_retain.stage_len = capture(s_retain.stage);
  U_MUTEX_LOCK(vm_retain_mutex, WAIT_FOREVER);
  memcpy(s_retain.last, s_retain.stage, s_retain.stage_len);
  s_retain.last_len = s_retain.stage_len;
  s_retain.dirty = false;
  s_retain.active = true;
  U_MUTEX_UNLOCK(vm_retain_mutex);
  s_retain.next_ms = 0;
  s_retain.retry = false;

  ERR_TRY(err);
  return NULL;
}

void vm_retain_on_pass(uint64_t now_ms) {
  if (!s_retain.active) return;
  if (s_retain.next_ms == 0) s_retain.next_ms = now_ms + RETAIN_PERIOD_MS;
  if (!s_retain.retry && now_ms < s_retain.next_ms) return;
  s_retain.next_ms = now_ms + RETAIN_PERIOD_MS;
  s_retain.stage_len = capture(s_retain.stage);
  s_retain.retry = !hand_over(false);
}

void vm_retain_save_stopped(void) {
  if (!s_retain.active) return;
  s_retain.stage_len = capture(s_retain.stage);
  (void)hand_over(true);
}

err_h vm_retain_clear(void) {
  U_MUTEX_LOCK(vm_retain_mutex, WAIT_FOREVER);
  s_retain.active = false;  // until the next start: the next start begins from uploaded values
  s_retain.dirty = false;
  s_retain.last_len = 0;
  err_h err = retain_erase();
  U_MUTEX_UNLOCK(vm_retain_mutex);
  ERR_TRY(err);
  return NULL;
}
