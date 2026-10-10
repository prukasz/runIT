#include "vm_retain.h"
#include <string.h>
#include "sys_settings.h"
#include "utils.h"
#include "vm_obj_access.h"
#include "vm_store.h"

#define OWNER OWNER_VM_RETAIN

#define VM_RETAIN_KEY "vm_retain"  // sys_settings record
#define RETAIN_HDR_LEN 2u          // u16 count
#define RETAIN_REC_FIXED 4u        // u8 name_len + u8 type + u16 size
#define RETAIN_PERIOD_MS ((uint64_t)CONFIG_VM_RETAIN_PERIOD_S * 1000u)

R_TASK_DEFINE(vm_retain_task_h, CONFIG_VM_RETAIN_TASK_STACK);

/* Guards s_last / s_last_len / s_dirty and the s_active change on clear. The
   VM task only ever try-locks it, so a flash write in progress never stalls a
   pass: the capture is handed over on a later pass instead. */
R_MUTEX_DEFINE(vm_retain_mutex);

/* Capture buffer: written by the VM task at a pass end, or by a control task
   while the VM is quiescent -- never both at once. */
static uint8_t s_stage[CONFIG_VM_RETAIN_MAX_BYTES];
static size_t s_stage_len;

/* Last snapshot handed to the retain task (under vm_retain_mutex). */
static uint8_t s_last[CONFIG_VM_RETAIN_MAX_BYTES];
static size_t s_last_len;
static bool s_dirty;  // s_last not written yet

static uint32_t s_budget;               // record bytes the loaded program reserved
static volatile bool s_restore_pending;  // loaded, not started yet
static volatile bool s_active;           // started, not torn down or cleared: saving allowed
static uint64_t s_next_ms;
static bool s_retry;  // hand-over failed (retain task busy): try on the next pass

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
  if (!R_MUTEX_LOCK(vm_retain_mutex, wait ? WAIT_FOREVER : 0)) return false;
  bool changed = false;
  if (s_active) {
    changed = s_stage_len != s_last_len || memcmp(s_stage, s_last, s_stage_len) != 0;
    if (changed) {
      memcpy(s_last, s_stage, s_stage_len);
      s_last_len = s_stage_len;
      s_dirty = true;
    }
  }
  R_MUTEX_UNLOCK(vm_retain_mutex);
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
static SE_MUST_USE err_h apply(const uint8_t* buf, size_t len) {
  if (len < RETAIN_HDR_LEN) SE_FAIL(ERR_VM_RETAIN_CORRUPT, .offset = 0);
  uint16_t count;
  memcpy(&count, buf, sizeof(count));
  size_t off = RETAIN_HDR_LEN;
  for (uint16_t i = 0; i < count; i++) {
    if (off + 1 > len) SE_FAIL(ERR_VM_RETAIN_CORRUPT, .offset = (uint32_t)off);
    const uint8_t name_len = buf[off++];
    if (name_len == 0 || name_len > VM_OBJ_NAME_MAX || off + name_len + 3u > len) {
      SE_FAIL(ERR_VM_RETAIN_CORRUPT, .offset = (uint32_t)off);
    }
    const char* name = (const char*)&buf[off];
    off += name_len;
    const uint8_t type = buf[off++];
    uint16_t size;
    memcpy(&size, &buf[off], sizeof(size));
    off += sizeof(size);
    if (off + size > len) SE_FAIL(ERR_VM_RETAIN_CORRUPT, .offset = (uint32_t)off);

    uint16_t id = 0;
    vm_obj_h o = find_retentive(name, name_len, &id);
    if (o) {
      if (o->head.d.obj_t != type || o->head.payload_size != size) {
        /* Renamed-in-place or retyped: keep the uploaded value. */
        SE_RAISE(ERR_VM_RETAIN_MISMATCH, .obj_id = id, .type = type, .size = size);
      } else {
        memcpy(o->payload, &buf[off], size);  // quiet: stored state, not new input
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
    R_MUTEX_LOCK(vm_retain_mutex, WAIT_FOREVER);
    err_h err = NULL;
    if (s_dirty) {
      err = sys_settings_store(VM_RETAIN_KEY, s_last, s_last_len);
      if (!err) s_dirty = false;  // on failure it stays dirty and goes out with the next change
    }
    R_MUTEX_UNLOCK(vm_retain_mutex);
    SE_REPORT(err);
  }
}

err_h vm_retain_init(void) {
  if (vm_retain_task_h == NULL) {
    R_TASK_START(vm_retain_task_h, vm_retain_task, NULL, CONFIG_VM_RETAIN_TASK_PRIO);
    if (vm_retain_task_h == NULL) SE_FAIL(ERR_BASE_NO_MEM, 0);
  }
  return NULL;
}

void vm_retain_on_open(void) {
  s_budget = 0;
  s_active = false;
  s_restore_pending = true;
}

void vm_retain_on_reset(void) {
  s_budget = 0;
  s_active = false;
  s_restore_pending = false;
}

err_h vm_retain_reserve(uint16_t obj_id, const vm_obj_head_t* head) {
  SE_CHECK_NOT_NULL(head);
  if (!head->f.retentive) return NULL;
  if (head->d.name_size == 0) SE_FAIL(ERR_VM_RETAIN_UNNAMED, .obj_id = obj_id);
  const uint32_t used = s_budget ? s_budget : RETAIN_HDR_LEN;
  const uint32_t need = used + RETAIN_REC_FIXED + head->d.name_size + head->payload_size;
  if (need > CONFIG_VM_RETAIN_MAX_BYTES) {
    SE_FAIL(ERR_VM_RETAIN_TOO_BIG, .obj_id = obj_id, .need = need, .max = CONFIG_VM_RETAIN_MAX_BYTES);
  }
  s_budget = need;
  return NULL;
}

err_h vm_retain_restore(void) {
  if (!s_restore_pending) return NULL;
  s_restore_pending = false;

  size_t len = 0;
  bool found = false;
  err_h err = sys_settings_load_blob(VM_RETAIN_KEY, s_stage, sizeof(s_stage), &len, &found);
  if (!err && found) err = apply(s_stage, len);

  /* What the objects hold now is what is stored: no rewrite on the first
     period unless something changes. Saving starts even if the restore
     failed, so the program's values are kept from here on. */
  s_stage_len = capture(s_stage);
  R_MUTEX_LOCK(vm_retain_mutex, WAIT_FOREVER);
  memcpy(s_last, s_stage, s_stage_len);
  s_last_len = s_stage_len;
  s_dirty = false;
  s_active = true;
  R_MUTEX_UNLOCK(vm_retain_mutex);
  s_next_ms = 0;
  s_retry = false;

  SE_TRY(err);
  return NULL;
}

void vm_retain_on_pass(uint64_t now_ms) {
  if (!s_active) return;
  if (s_next_ms == 0) s_next_ms = now_ms + RETAIN_PERIOD_MS;
  if (!s_retry && now_ms < s_next_ms) return;
  s_next_ms = now_ms + RETAIN_PERIOD_MS;
  s_stage_len = capture(s_stage);
  s_retry = !hand_over(false);
}

void vm_retain_save_stopped(void) {
  if (!s_active) return;
  s_stage_len = capture(s_stage);
  (void)hand_over(true);
}

err_h vm_retain_clear(void) {
  R_MUTEX_LOCK(vm_retain_mutex, WAIT_FOREVER);
  s_active = false;  // until the next start: the next start begins from uploaded values
  s_dirty = false;
  s_last_len = 0;
  err_h err = sys_settings_erase(VM_RETAIN_KEY);
  R_MUTEX_UNLOCK(vm_retain_mutex);
  SE_TRY(err);
  return NULL;
}
