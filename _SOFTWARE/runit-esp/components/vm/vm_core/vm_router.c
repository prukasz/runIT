#include "vm_router.h"
#include "vm_err.h"

#include "vm_clock.h"
#include "vm_event.h"
#include "vm_exec.h"
#include "vm_loader.h"
#include "vm_override.h"
#include "vm_retain.h"
#include "vm_stats.h"
#include "vm_sub.h"
#include "vm_wire.h"

/* The ops of VM_CORE.MD "Remote". Each one parses its frame with the packed records of vm_wire.h (a bounds check
   before every copy: everything from the wire is untrusted) and calls the loader / supervisor. A truncated frame is
   ERR_VM_LOAD_SHORT_RECORD. The record layouts are shared with the telemetry encoder (vm_sub.c). */

// The op: `body` is the payload after the op key.
#define VM_OP_FN(name)                                                       \
  static err_h name##_body(const uint8_t* body, size_t len);                 \
  static err_h name(CMD_F_PARAMS) {                                          \
    (void)ctx;                                                               \
    (void)out;                                                               \
    (void)out_cap;                                                           \
    (void)out_len;                                                           \
    return name##_body(in + 1, in_len - 1);                                  \
  }                                                                          \
  static err_h name##_body(const uint8_t* body, size_t len)

static inline MUST_USE err_h need(uint8_t op, size_t off, size_t len, size_t want) {
  if (unlikely((uint32_t)off + (uint32_t)want > (uint32_t)len)) {
    return ERR_NEW(ERR_VM_LOAD_SHORT_RECORD, .packet = op, .need = (uint16_t)want,
                   .got = (uint16_t)(len > off ? len - off : 0));
  }
  return NULL;
}

/* Fail closed: a failing load op discards the whole program (vm_loader_abort), whatever the failure (a truncated
   record, a bad id, no memory, a rejected block). `armed` is false for an op that was only refused without touching
   the program (it arrived while the VM runs). */
static inline MUST_USE err_h load_result(err_h err, uint8_t op, bool armed) {
  if (likely(!err) || !armed) return err;
  return vm_loader_abort(err, op);
}

// While the VM runs, ADD_OBJECTS / ADD_ACCESSORS / ADD_BLOCK are refused by the loader and DATA is a runtime
// write: none of them touches the loaded program, so a failure there doesn't discard it. OPEN is the one op that
// replaces a running program, so it always counts.
static inline bool loading(void) {
  return vm_exec_mode() == VM_RUN_STOPPED;
}

VM_OP_FN(op_reset) {
  (void)body;
  (void)len;
  return vm_loader_reset();
}

VM_OP_FN(op_open) {
  vm_wire_open_t rec;
  ERR_TRY(need(VM_OP_OPEN, 0, len, sizeof(rec)));
  memcpy(&rec, body, sizeof(rec));
  // Returned as is: a failed open already comes back wrapped in ERR_VM_LOAD_ABORTED (the store is empty).
  return vm_loader_open(rec.obj_cnt, rec.acc_cnt, rec.blk_cnt, rec.total_size);
}

static err_h add_objects(const uint8_t* body, size_t len) {
  ERR_TRY(need(VM_OP_ADD_OBJECTS, 0, len, 1));
  uint8_t n = body[0];
  size_t off = 1;
  for (uint8_t i = 0; i < n; i++) {
    vm_wire_obj_t rec;
    ERR_TRY(need(VM_OP_ADD_OBJECTS, off, len, sizeof(rec)));
    memcpy(&rec, body + off, sizeof(rec));
    off += sizeof(rec);

    vm_obj_head_t head = vm_wire_head_unpack(&rec.head);

    uint8_t name_len = head.d.name_size;
    ERR_TRY(need(VM_OP_ADD_OBJECTS, off, len, name_len));
    const char* name = name_len ? (const char*)(body + off) : NULL;
    off += name_len;

    ERR_TRY(vm_loader_add_obj(rec.id, &head, name));
  }
  return NULL;
}

VM_OP_FN(op_add_objects) {
  return load_result(add_objects(body, len), VM_OP_ADD_OBJECTS, loading());
}

static err_h set_data(const uint8_t* body, size_t len) {
  ERR_TRY(need(VM_OP_DATA, 0, len, 1));
  uint8_t n = body[0];
  size_t off = 1;
  for (uint8_t i = 0; i < n; i++) {
    vm_wire_data_t rec;
    ERR_TRY(need(VM_OP_DATA, off, len, sizeof(rec)));
    memcpy(&rec, body + off, sizeof(rec));
    off += sizeof(rec);

    ERR_TRY(need(VM_OP_DATA, off, len, rec.byte_len));
    if (vm_exec_mode() != VM_RUN_STOPPED) {
      ERR_TRY(vm_override_post(rec.id, rec.start_idx, body + off, rec.byte_len));  // applied at the next pass start
    } else {
      ERR_TRY(vm_loader_set_data(rec.id, rec.start_idx, body + off, rec.byte_len));
    }
    off += rec.byte_len;
  }
  return NULL;
}

VM_OP_FN(op_set_data) {
  return load_result(set_data(body, len), VM_OP_DATA, loading());
}

static err_h add_accessors(const uint8_t* body, size_t len) {
  ERR_TRY(need(VM_OP_ADD_ACCESSORS, 0, len, 1));
  uint8_t n = body[0];
  size_t off = 1;
  for (uint8_t i = 0; i < n; i++) {
    vm_wire_acc_t rec;
    ERR_TRY(need(VM_OP_ADD_ACCESSORS, off, len, sizeof(rec)));
    memcpy(&rec, body + off, sizeof(rec));
    off += sizeof(rec);

    ERR_TRY(need(VM_OP_ADD_ACCESSORS, off, len, rec.idx_len));
    ERR_TRY(vm_loader_add_accessor(rec.acc_id, rec.root_obj_id, rec.idx_count, body + off, rec.idx_len));
    off += rec.idx_len;
  }
  return NULL;
}

VM_OP_FN(op_add_accessors) {
  return load_result(add_accessors(body, len), VM_OP_ADD_ACCESSORS, loading());
}

// The pin id arrays of a block frame: `count` u16 at `off`, bounds-checked, copied out (the frame is unaligned).
static err_h pin_ids(const uint8_t* body, size_t len, size_t* off, uint16_t* dst, uint8_t count) {
  ERR_TRY(need(VM_OP_ADD_BLOCK, *off, len, (size_t)count * 2));
  memcpy(dst, body + *off, (size_t)count * sizeof(*dst));
  *off += (size_t)count * 2;
  return NULL;
}

static err_h add_block(const uint8_t* body, size_t len) {
  vm_wire_block_t rec;
  ERR_TRY(need(VM_OP_ADD_BLOCK, 0, len, sizeof(rec)));
  memcpy(&rec, body, sizeof(rec));
  vm_block_cfg_t cfg = {
      .block_idx = rec.block_idx,
      .block_type = rec.block_type,
      .in_cnt = rec.in_cnt,
      .q_cnt = rec.q_cnt,
      .en_cnt = rec.en_cnt,
      .en_mode = rec.en_mode,
      .on_error = rec.on_error,
      .custom_len = rec.custom_len,
      .eno_obj_id = rec.eno_obj_id,
  };
  size_t off = sizeof(rec);

  if (cfg.in_cnt > CONFIG_VM_BLOCK_MAX_IN || cfg.q_cnt > CONFIG_VM_BLOCK_MAX_OUT || cfg.en_cnt > CONFIG_VM_BLOCK_MAX_EN) {
    return ERR_NEW(ERR_VM_BLK_BAD_SHAPE, .blk_id = cfg.block_idx, .in_cnt = cfg.in_cnt, .q_cnt = cfg.q_cnt);
  }

  uint16_t in_ids[CONFIG_VM_BLOCK_MAX_IN];
  ERR_TRY(pin_ids(body, len, &off, in_ids, cfg.in_cnt));
  cfg.in_acc_ids = cfg.in_cnt ? in_ids : NULL;

  uint16_t out_ids[CONFIG_VM_BLOCK_MAX_OUT];
  ERR_TRY(pin_ids(body, len, &off, out_ids, cfg.q_cnt));
  cfg.out_obj_ids = cfg.q_cnt ? out_ids : NULL;

  uint16_t en_ids[CONFIG_VM_BLOCK_MAX_EN];
  ERR_TRY(pin_ids(body, len, &off, en_ids, cfg.en_cnt));
  cfg.en_acc_ids = cfg.en_cnt ? en_ids : NULL;

  ERR_TRY(need(VM_OP_ADD_BLOCK, off, len, cfg.custom_len));
  cfg.custom_data = cfg.custom_len ? (body + off) : NULL;
  return vm_loader_add_block(rec.blk_id, &cfg);
}

VM_OP_FN(op_add_block) {
  return load_result(add_block(body, len), VM_OP_ADD_BLOCK, loading());
}

VM_OP_FN(op_subscribe) {
  ERR_TRY(need(VM_OP_SUBSCRIBE, 0, len, 1));
  uint8_t n = body[0];
  if (n > 0) {
    ERR_TRY(need(VM_OP_SUBSCRIBE, 1, len, (size_t)n * sizeof(vm_wire_sub_t)));
  }
  return vm_sub_handle_packet(body, len);
}

VM_OP_FN(op_exec) {
  vm_wire_exec_t rec;
  if (len != sizeof(rec)) {
    return ERR_NEW(ERR_VM_LOAD_SHORT_RECORD, .packet = VM_OP_EXEC, .need = sizeof(rec), .got = (uint16_t)len);
  }
  memcpy(&rec, body, sizeof(rec));
  uint8_t cmd = rec.command;
  if (cmd == VM_EXEC_RESET) {
    return vm_loader_reset();
  }
  if (cmd == VM_EXEC_RETAIN_CLEAR) {
    return vm_retain_clear();
  }
  // A program starts only when every slot OPEN declared was uploaded; otherwise it is discarded.
  if (cmd == VM_EXEC_NORMAL_MODE || cmd == VM_EXEC_SCAN_MODE || cmd == VM_EXEC_BLOCK_MODE) {
    err_h incomplete = vm_loader_verify_complete();
    if (incomplete) return vm_loader_abort(incomplete, VM_OP_EXEC);
  }
  return vm_exec_control((vm_exec_command_e)cmd);
}

VM_OP_FN(op_speed) {
  // The factor alone (2 bytes) or with the step (4); nothing is changed unless the whole body is valid.
  vm_wire_speed_t rec = {.factor = 0, .step_ms = vm_clock_step_ms()};
  if (len != sizeof(rec.factor) && len != sizeof(rec)) {
    return ERR_NEW(ERR_VM_LOAD_SHORT_RECORD, .packet = VM_OP_SPEED, .need = sizeof(rec), .got = (uint16_t)len);
  }
  memcpy(&rec, body, len);
  ERR_CHECK(rec.step_ms <= VM_CLOCK_STEP_MS_MAX, ERR_VM_BAD_ARG);
  ERR_TRY(vm_clock_set_slowdown(rec.factor));
  return vm_clock_set_step_ms(rec.step_ms);
}

// Reads only: never changes the program or the run, so it is safe at any time (also while a pass runs).
static err_h op_status(CMD_F_PARAMS) {
  (void)ctx;
  (void)in;
  (void)in_len;
  const vm_exec_status_t run = vm_exec_status();
  const vm_exec_fault_status_t fault = vm_exec_fault_status();
  err_h incomplete = vm_loader_verify_complete();
  const bool loaded = vm_loader_state() == VM_LOAD_OPEN;
  const vm_wire_status_t r = {
      .mode = (uint8_t)run.mode,
      .load_state = (uint8_t)vm_loader_state(),
      .ready = loaded && incomplete == NULL,
      .fault_latched = fault.latched,
      .fault_device = fault.device_id,
      .slowdown = vm_clock_slowdown(),
      .fault_root_tag = fault.root_tag,
      .pass_count = vm_exec_pass_count(),
      .last_pass_us = vm_exec_last_pass_us(),
      .arena_used = vm_loader_used(),
      // Parked before a block: that one. In block mode between two passes no pass has started: the next NEXT runs block 0.
      .next_block = run.waiting ? run.next_block : (run.mode == VM_RUN_BLOCK && !run.scan_active) ? 0 : UINT16_MAX,
  };
  if (incomplete) err_free(incomplete);
  return cmd_reply(out, out_cap, out_len, &r, sizeof(r));
}

// OPEN replaces a running program, so it always counts for the fail-closed rule.
static err_h op_open_closed(CMD_F_PARAMS) {
  return load_result(op_open(ctx, in, in_len, out, out_cap, out_len), VM_OP_OPEN, true);
}

err_h vm_init(void) {
  ERR_TRY(vm_event_init());
  ERR_TRY(vm_sub_init());
  ERR_TRY(vm_retain_init());
  return vm_exec_start();
}

err_h vm_stop_on_fault(uint16_t device_id) {
  (void)vm_exec_fault_latch(device_id, 0);  // requests the stop at once; the root tag is not known here
  err_h err = vm_exec_stop();
  if (err != NULL && err->tag == ERR_VM_EXEC_SELF_BARRIER) {  // called from the pass itself: the latch stops it
    err_free(err);
    return NULL;
  }
  return err;
}

static const router_op_t k_ops[] = {
    ROUTER_W(VM_OP_RESET, op_reset),
    ROUTER_W(VM_OP_OPEN, op_open_closed),
    ROUTER_W(VM_OP_ADD_OBJECTS, op_add_objects),
    ROUTER_W(VM_OP_DATA, op_set_data),
    ROUTER_W(VM_OP_ADD_ACCESSORS, op_add_accessors),
    ROUTER_W(VM_OP_ADD_BLOCK, op_add_block),
    ROUTER_W(VM_OP_SUBSCRIBE, op_subscribe),
    ROUTER_W(VM_OP_EXEC, op_exec),
    ROUTER_W(VM_OP_SPEED, op_speed),
    ROUTER_R(VM_OP_STATUS, op_status),
};

ROUTER_MODULE(vm_router, k_ops)
