#pragma once
/**
 * @file dec_vm_loader.h
 * @brief Header-only decoder table for the "VM Program Load" packet class (0x04).
 *
 * Wire format handled by this class:
 * @code
 *   [0x04] [0xYY] [ payload... ]
 *    class  packet
 * @endcode
 *
 * Record layouts are the packed structs of vm_wire.h (published to
 * data-structures/vm/vm-program.generated.json): a record's fixed part is
 * copied into its struct after a bounds check (`dec_vm_need`), its variable
 * tails are walked with the same check. Truncated frames emit
 * `ERR_VM_LOAD_SHORT_RECORD`. Little-endian. Blocks (`0x45`) are uploaded in
 * execution order.
 */

#include <stdint.h>
#include <string.h>
#include "esp_log.h"
#include "sys_error.h"
#include "utils.h"
#include "vm_exec.h"
#include "vm_loader.h"
#include "vm_override.h"
#include "vm_retain.h"
#include "vm_sub.h"
#include "vm_wire.h"

#undef OWNER
#define OWNER OWNER_DEC_VM_LOADER

/** @brief ESP log tag used by every decoder in this table. */
#define DEC_VM_LOADER_TAG "dec_vm_loader"

#define HEADER_packet_vm_reset     0x40
#define HEADER_packet_vm_open      0x41
#define HEADER_packet_vm_add_objs  0x42
#define HEADER_packet_vm_set_data  0x43
#define HEADER_packet_vm_add_acc   0x44
#define HEADER_packet_vm_add_block 0x45
#define HEADER_packet_vm_subscribe 0x47
#define HEADER_packet_vm_exec      0x48

/* The VM core (vm_sub.c) can't include this decoder, so it has its own Kconfig
   copies of the bytes it shares with it. They must stay equal: telemetry frames
   reuse the upload layouts, and the app parses both directions with one
   decoder. */
_Static_assert(CONFIG_TX_PACKET_CLASS_VM_LOADER == CONFIG_RX_PACKET_CLASS_VM_LOADER, "telemetry frames carry the loader class byte");
_Static_assert(CONFIG_RX_PACKET_HEADER_VM_SUBSCRIBE == HEADER_packet_vm_subscribe, "vm_sub's subscribe header is the decoder's");
_Static_assert(CONFIG_TX_PACKET_HEADER_VM_SET_DATA == HEADER_packet_vm_set_data, "telemetry values reuse the 0x43 layout and header");
_Static_assert(CONFIG_TX_PACKET_HEADER_VM_DESCRIBE == HEADER_packet_vm_add_objs, "telemetry describes reuse the 0x42 layout and header");

/* ========================================================================= */
/* Cursor & Little-Endian Stream Helpers                                     */
/* ========================================================================= */

static inline void dec_vm_u16_array(uint16_t* dst, const uint8_t* src, uint8_t count) {
  memcpy(dst, src, (size_t)count * sizeof(*dst));
}

static inline SE_MUST_USE err_h dec_vm_need(uint8_t pkt, size_t off, size_t len, size_t need) {
  if (unlikely((uint32_t)off + (uint32_t)need > (uint32_t)len)) {
    SE_FAIL(ERR_VM_LOAD_SHORT_RECORD, .packet = pkt, .need = (uint16_t)need, .got = (uint16_t)(len > off ? len - off : 0));
  }
  return NULL;
}

/* ========================================================================= */
/* Packet Decoder Functions                                                  */
/* ========================================================================= */

/**
 * @brief Packet 0x40: VM Reset
 *
 * - **Wire Layout**: None (0 payload bytes).
 * - **Action**:
 *   - Enforces lifecycle barrier: stops pass admission and waits for active pass to finish.
 *   - Reclaims dynamic heap objects, clears registries, and resets bump arena.
 *   - Leaves execution stopped in fail-closed state (`VM_LOAD_EMPTY`).
 */
static inline SE_MUST_USE err_h decoder_packet_vm_reset(void) {
  SE_TRY(vm_loader_reset());
  DBG(ESP_LOGI(DEC_VM_LOADER_TAG, "storage reset"););
  return NULL;
}

/**
 * @brief Packet 0x41: VM Program Open
 *
 * - **Wire Layout**: `vm_wire_open_t` (10 bytes).
 * - **Action**:
 *   - Validates memory availability against DRAM limits before modifying active state.
 *   - Tears down prior program and re-arms registries and arena at declared capacity.
 *   - Transitions loader state to `VM_LOAD_OPEN`.
 */
static inline SE_MUST_USE err_h decoder_packet_vm_open(const uint8_t* body, size_t len) {
  vm_wire_open_t rec;
  SE_TRY(dec_vm_need(HEADER_packet_vm_open, 0, len, sizeof(rec)));
  memcpy(&rec, body, sizeof(rec));
  SE_TRY(vm_loader_open(rec.obj_cnt, rec.acc_cnt, rec.blk_cnt, rec.total_size));
  DBG(ESP_LOGI(DEC_VM_LOADER_TAG, "open: %u objects, %u accessors, %u blocks, %lu bytes", rec.obj_cnt, rec.acc_cnt, rec.blk_cnt, (unsigned long)rec.total_size););
  return NULL;
}

/**
 * @brief Packet 0x42: Add Objects Batch
 *
 * - **Wire Layout**:
 *   - `u8 n`: Number of object records in frame
 *   - `n ×` `vm_wire_obj_t`: `u16 id`, `vm_obj_head_t` (4 bytes, the ESP32 GCC
 *     bitfield ABI), then `char name[head.d.name_size]` (unterminated).
 * - **Action**:
 *   - Carves 4-byte aligned chunk in bump arena, zeroes payload memory, and stores header.
 *   - Binds object pointer into registry index `id`.
 *   - Appends tag name to object tail.
 */
static inline SE_MUST_USE err_h decoder_packet_vm_add_objs(const uint8_t* body, size_t len) {
  SE_TRY(dec_vm_need(HEADER_packet_vm_add_objs, 0, len, 1));
  uint8_t n = body[0];
  size_t  off = 1;

  for (uint8_t i = 0; i < n; i++) {
    vm_wire_obj_t rec;
    SE_TRY(dec_vm_need(HEADER_packet_vm_add_objs, off, len, sizeof(rec)));
    memcpy(&rec, body + off, sizeof(rec));
    off += sizeof(rec);

    vm_obj_head_t head;
    memcpy(&head, rec.head, VM_OBJ_HEAD_WIRE_SIZE);

    uint8_t name_len = head.d.name_size;
    SE_TRY(dec_vm_need(HEADER_packet_vm_add_objs, off, len, name_len));
    const char* name = name_len ? (const char*)(body + off) : NULL;
    off += name_len;

    SE_TRY(vm_loader_add_obj(rec.id, &head, name));
  }
  return NULL;
}

/**
 * @brief Packet 0x43: Set Object Data / Runtime Override
 *
 * - **Wire Layout**:
 *   - `u8 n`: Number of data chunk records in frame
 *   - `n ×` `vm_wire_data_t`: `u16 id`, `u16 start_idx` (elements), `u16 byte_len`,
 *     then `data[byte_len]`: values, or child `u16` IDs for `VM_OBJ_PTR`.
 * - **Action**:
 *   - When VM is stopped (`VM_RUN_STOPPED`): Writes bytes directly via `vm_loader_set_data()`.
 *     For pointer containers, validates child IDs and links children in arena.
 *   - When VM is running: Enqueues variable update via `vm_override_post()`, applied
 *     atomically at supervisor cycle drain.
 */
static inline SE_MUST_USE err_h decoder_packet_vm_set_data(const uint8_t* body, size_t len) {
  SE_TRY(dec_vm_need(HEADER_packet_vm_set_data, 0, len, 1));
  uint8_t n = body[0];
  size_t  off = 1;

  for (uint8_t i = 0; i < n; i++) {
    vm_wire_data_t rec;
    SE_TRY(dec_vm_need(HEADER_packet_vm_set_data, off, len, sizeof(rec)));
    memcpy(&rec, body + off, sizeof(rec));
    off += sizeof(rec);

    SE_TRY(dec_vm_need(HEADER_packet_vm_set_data, off, len, rec.byte_len));
    if (vm_exec_mode() != VM_RUN_STOPPED) {
      SE_TRY(vm_override_post(rec.id, rec.start_idx, body + off, rec.byte_len));
    } else {
      SE_TRY(vm_loader_set_data(rec.id, rec.start_idx, body + off, rec.byte_len));
    }
    off += rec.byte_len;
  }
  return NULL;
}

/**
 * @brief Packet 0x44: Add Accessors Batch
 *
 * - **Wire Layout**:
 *   - `u8 n`: Number of accessor records in frame
 *   - `n ×` `vm_wire_acc_t`: `u16 acc_id`, `u16 root_obj_id`, `u8 idx_count`,
 *     `u8 idx_len`, then `idx_len` bytes of index steps (`vm_wire_idx_*_t`,
 *     parsed by the loader).
 * - **Action**:
 *   - Allocates accessor descriptor and trailing index array in bump arena.
 *   - Binds descriptor to registry index `acc_id`.
 *   - Builds resolution cache (`vm_accessor_cache_build`) for static literal paths.
 */
static inline SE_MUST_USE err_h decoder_packet_vm_add_acc(const uint8_t* body, size_t len) {
  SE_TRY(dec_vm_need(HEADER_packet_vm_add_acc, 0, len, 1));
  uint8_t n = body[0];
  size_t  off = 1;

  for (uint8_t i = 0; i < n; i++) {
    vm_wire_acc_t rec;
    SE_TRY(dec_vm_need(HEADER_packet_vm_add_acc, off, len, sizeof(rec)));
    memcpy(&rec, body + off, sizeof(rec));
    off += sizeof(rec);

    SE_TRY(dec_vm_need(HEADER_packet_vm_add_acc, off, len, rec.idx_len));
    SE_TRY(vm_loader_add_accessor(rec.acc_id, rec.root_obj_id, rec.idx_count, body + off, rec.idx_len));
    off += rec.idx_len;
  }
  return NULL;
}

/**
 * @brief Packet 0x45: Add Block (Single Block per Frame)
 *
 * - **Wire Layout**:
 *   - `vm_wire_block_t` (14 bytes), then:
 *     - `in_cnt  × u16` : Input accessor IDs (`VM_BLOCK_NO_ID` for unwired pins)
 *     - `q_cnt   × u16` : Output object IDs (automatically marked `usr_protected`)
 *     - `en_cnt  × u16` : Enable accessor IDs
 *     - `custom_len × u8`: Initial private configuration / bytecode / constant data
 * - **Action**:
 *   - Validates pin counts (`VM_BLOCK_MAX_IN`, `VM_BLOCK_MAX_OUT`, `VM_BLOCK_MAX_EN`).
 *   - Allocates block struct in bump arena, binds registry ID, and wires pins.
 *   - Copies private state into block custom data memory.
 */
static inline SE_MUST_USE err_h decoder_packet_vm_add_block(const uint8_t* body, size_t len) {
  vm_wire_block_t rec;
  SE_TRY(dec_vm_need(HEADER_packet_vm_add_block, 0, len, sizeof(rec)));
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
    SE_FAIL(ERR_VM_BLK_BAD_SHAPE, .blk_id = cfg.block_idx, .in_cnt = cfg.in_cnt, .q_cnt = cfg.q_cnt);
  }

  uint16_t in_ids[CONFIG_VM_BLOCK_MAX_IN];
  SE_TRY(dec_vm_need(HEADER_packet_vm_add_block, off, len, (size_t)cfg.in_cnt * 2));
  dec_vm_u16_array(in_ids, body + off, cfg.in_cnt);
  off += (size_t)cfg.in_cnt * 2;
  cfg.in_acc_ids = cfg.in_cnt ? in_ids : NULL;

  uint16_t out_ids[CONFIG_VM_BLOCK_MAX_OUT];
  SE_TRY(dec_vm_need(HEADER_packet_vm_add_block, off, len, (size_t)cfg.q_cnt * 2));
  dec_vm_u16_array(out_ids, body + off, cfg.q_cnt);
  off += (size_t)cfg.q_cnt * 2;
  cfg.out_obj_ids = cfg.q_cnt ? out_ids : NULL;

  uint16_t en_ids[CONFIG_VM_BLOCK_MAX_EN];
  SE_TRY(dec_vm_need(HEADER_packet_vm_add_block, off, len, (size_t)cfg.en_cnt * 2));
  dec_vm_u16_array(en_ids, body + off, cfg.en_cnt);
  off += (size_t)cfg.en_cnt * 2;
  cfg.en_acc_ids = cfg.en_cnt ? en_ids : NULL;

  SE_TRY(dec_vm_need(HEADER_packet_vm_add_block, off, len, cfg.custom_len));
  cfg.custom_data = cfg.custom_len ? (body + off) : NULL;
  SE_TRY(vm_loader_add_block(rec.blk_id, &cfg));
  return NULL;
}

/**
 * @brief Packet 0x47: Subscribe Object Telemetry
 *
 * - **Wire Layout**:
 *   - `u8 count` (`0` clears all subscriptions), then `count ×` `vm_wire_sub_t`
 *     (`u16` object ID, `VM_OBJ_ID_DYN_BIT` for heap objects); more than
 *     `CONFIG_VM_SUB_MAX_SUBSCRIBERS` is refused
 * - **Action**:
 *   - Replaces the subscription list; everything reachable is sent in full once.
 *   - Then sends every object whose value changed, after each completed pass
 *     (0x43 records; 0x42 describe records for heap objects). VM.MD
 *     "Subscriptions and telemetry".
 */
static inline SE_MUST_USE err_h decoder_packet_vm_subscribe(const uint8_t* body, size_t len) {
  SE_TRY(dec_vm_need(HEADER_packet_vm_subscribe, 0, len, 1));
  uint8_t n = body[0];
  if (n > 0) {
    SE_TRY(dec_vm_need(HEADER_packet_vm_subscribe, 1, len, (size_t)n * sizeof(vm_wire_sub_t)));
  }
  return vm_sub_handle_packet(body, len);
}

/**
 * @brief Packet 0x48: Execution Control
 *
 * - **Wire Layout**: `vm_wire_exec_t` (1 byte):
 *   - `u8 command`: `vm_exec_command_e` enum value:
 *     - 0: scan mode, 1: once, 2: block mode, 3: next
 *     - 4: rewind without unloading, 5: normal mode
 *     - 6: pause, 7: resume, 8: full loader reset
 *     - 9: acknowledge a latched critical device fault
 *     - 10: forget retained values (vm_retain_clear)
 * - **Action**:
 *   - Routes command directly to `vm_exec_control()`, or performs full `vm_loader_reset()`.
 */
static inline SE_MUST_USE err_h decoder_packet_vm_exec(const uint8_t* body, size_t len) {
  vm_wire_exec_t rec;
  if (len != sizeof(rec)) {
    SE_FAIL(ERR_VM_LOAD_SHORT_RECORD, .packet = HEADER_packet_vm_exec, .need = sizeof(rec), .got = (uint16_t)len);
  }
  memcpy(&rec, body, sizeof(rec));
  uint8_t cmd = rec.command;
  if (cmd == VM_EXEC_RESET) {
    SE_TRY(vm_loader_reset());
    DBG(ESP_LOGI(DEC_VM_LOADER_TAG, "vm execution reset"););
    return NULL;
  }
  if (cmd == VM_EXEC_RETAIN_CLEAR) {
    SE_TRY(vm_retain_clear());
    return NULL;
  }
  return vm_exec_control((vm_exec_command_e)cmd);
}

/* ========================================================================= */
/* Class Dispatcher Function                                                 */
/* ========================================================================= */

/**
 * @brief Class handler for RX_PACKET_CLASS_VM_LOADER (0x04).
 *
 * @param data Frame bytes with class byte stripped (data[0] is packet byte 0xYY).
 * @param len Total number of bytes available at @p data.
 * @return err_h NULL on success, ERR_INTERFACE_UNKNOWN_PACKET for unknown header,
 *               or the decoder's returned error chain.
 */
static inline SE_MUST_USE err_h dec_vm_loader_decode(const uint8_t* data, size_t len) {
  if (len == 0) {
    SE_FAIL(ERR_INTERFACE_SHORT_FRAME, .got = 0, .need = 1);
  }

  const uint8_t* body = data + 1;
  size_t         body_len = len - 1;

  switch (data[0]) {
    case HEADER_packet_vm_reset:
      return decoder_packet_vm_reset();
    case HEADER_packet_vm_open:
      return decoder_packet_vm_open(body, body_len);
    case HEADER_packet_vm_add_objs:
      return decoder_packet_vm_add_objs(body, body_len);
    case HEADER_packet_vm_set_data:
      return decoder_packet_vm_set_data(body, body_len);
    case HEADER_packet_vm_add_acc:
      return decoder_packet_vm_add_acc(body, body_len);
    case HEADER_packet_vm_add_block:
      return decoder_packet_vm_add_block(body, body_len);
    case HEADER_packet_vm_subscribe:
      return decoder_packet_vm_subscribe(body, body_len);
    case HEADER_packet_vm_exec:
      return decoder_packet_vm_exec(body, body_len);
    default:
      ESP_LOGW(DEC_VM_LOADER_TAG, "unknown packet header 0x%02X", data[0]);
      SE_FAIL(ERR_INTERFACE_UNKNOWN_PACKET, .class_header = CONFIG_RX_PACKET_CLASS_VM_LOADER, .packet_header = data[0]);
  }
}
