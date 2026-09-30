#include "vm_override.h"
#include "utils.h"
#include "vm_obj_access.h"
#include "vm_obj_build.h"
#include "vm_store.h"

// This file's DBG() calls fire on CONFIG_DBG_GLOBAL or this component's own
// switch (components/utils/Kconfig) - see DBG()'s doc comment in utils.h.
#define DBG_ENABLE CONFIG_DBG_ENABLE_VM

#define OWNER OWNER_VM_EXEC

static const char* TAG = "vm_override";

// Static ring buffer initialized at startup via constructor macro
R_RINGBUFFER_DEFINE(s_override_rb, CONFIG_VM_OVERRIDE_BUF_SIZE, RINGBUF_TYPE_NOSPLIT);

static SE_MUST_USE err_h validate_override_target(uint16_t id, uint16_t start_idx, uint16_t len,
                                      vm_obj_h* out_obj, uint8_t* out_width) {
  vm_obj_h obj = vm_obj_get_by_id(id);
  if (!obj) {
    SE_FAIL(ERR_VM_ACCESSOR_UNKNOWN_ID, .id = id);
  }
  if ((vm_obj_t_e)obj->head.d.obj_t == VM_OBJ_PTR) {
    SE_FAIL(ERR_VM_OVERRIDE_PTR_UNSUPPORTED, .obj_id = id);
  }
  if (!obj->head.f.mutable) {
    SE_FAIL(ERR_VM_OBJ_NOT_MUTABLE, .obj_id = id);
  }
  if (obj->head.f.usr_protected) {
    SE_FAIL(ERR_VM_OBJ_USR_PROTECTED, .obj_id = id);
  }

  uint8_t w = vm_obj_get_type_size(obj);
  uint16_t items = vm_obj_get_items_cnt(obj);
  if (w == 0 || (len % w) != 0) {
    SE_FAIL(ERR_VM_LOAD_DATA_RANGE, .id = id, .start_idx = start_idx, .len = len, .items = items);
  }
  uint16_t n = len / w;
  if ((uint32_t)start_idx + n > items) {
    SE_FAIL(ERR_VM_LOAD_DATA_RANGE, .id = id, .start_idx = start_idx, .len = n, .items = items);
  }

  if (out_obj) *out_obj = obj;
  if (out_width) *out_width = w;
  return NULL;
}

static uint16_t size_u16_sat(size_t size) {
  return (uint16_t)(size > UINT16_MAX ? UINT16_MAX : size);
}

static void report_bad_record(uint16_t id, uint16_t declared_len, size_t item_size) {
  SE_RAISE(ERR_VM_OVERRIDE_BAD_RECORD, .obj_id = id, .declared_len = declared_len,
              .item_size = size_u16_sat(item_size));
}

err_h vm_override_post(uint16_t id, uint16_t start_idx, const uint8_t* data, uint16_t len) {
  if (!data && len > 0) {
    SE_FAIL(ERR_NULL_PTR, 0);
  }
  SE_TRY(validate_override_target(id, start_idx, len, NULL, NULL));

  if (unlikely(!s_override_rb)) {
    SE_FAIL(ERR_VM_ALLOC_EXHAUSTED, .requested = len, .remaining = 0);
  }

  size_t rec_size = sizeof(vm_override_record_t) + len;
  void* item_mem = NULL;
  // Non-blocking acquire from caller context (decoder task)
  if (xRingbufferSendAcquire(s_override_rb, &item_mem, rec_size, 0) != pdTRUE || !item_mem) {
    SE_FAIL(ERR_VM_ALLOC_EXHAUSTED, .requested = rec_size, .remaining = 0);
  }

  vm_override_record_t* rec = (vm_override_record_t*)item_mem;
  rec->id = id;
  rec->start_idx = start_idx;
  rec->len = len;
  if (len > 0) {
    memcpy(rec->data, data, len);
  }

  if (xRingbufferSendComplete(s_override_rb, item_mem) != pdTRUE) {
    SE_FAIL(ERR_VM_ALLOC_EXHAUSTED, .requested = rec_size, .remaining = 0);
  }

  return NULL;
}

void vm_override_drain(void) {
  if (unlikely(!s_override_rb)) return;

  size_t item_size = 0;
  void* item = NULL;
  uint16_t applied_cnt = 0;

  while ((item = xRingbufferReceive(s_override_rb, &item_size, 0)) != NULL) {
    if (item_size < sizeof(vm_override_record_t)) {
      report_bad_record(VM_ID_NONE, 0, item_size);
      vRingbufferReturnItem(s_override_rb, item);
      continue;
    }

    const vm_override_record_t* rec = (const vm_override_record_t*)item;
    size_t required = sizeof(vm_override_record_t) + (size_t)rec->len;
    if (required > item_size) {
      report_bad_record(rec->id, rec->len, item_size);
      vRingbufferReturnItem(s_override_rb, item);
      continue;
    }

    vm_obj_h obj = NULL;
    uint8_t w = 0;
    err_h validation = validate_override_target(rec->id, rec->start_idx, rec->len, &obj, &w);
    if (validation) {
      SE_push_to_handler(validation);
    } else if (rec->len > 0) {
      memcpy(obj->payload + (size_t)rec->start_idx * w, rec->data, rec->len);
      obj->head.f.upd = 1;
      applied_cnt++;
    }
    vRingbufferReturnItem(s_override_rb, item);
  }

  DBG(if (applied_cnt > 0) { ESP_LOGI(TAG, "overrides applied: %u records", applied_cnt); });
}

void vm_override_reset(void) {
  if (unlikely(!s_override_rb)) return;

  // Drain and return all items in the ring buffer
  size_t item_size = 0;
  void* item = NULL;
  while ((item = xRingbufferReceive(s_override_rb, &item_size, 0)) != NULL) {
    vRingbufferReturnItem(s_override_rb, item);
  }
}
