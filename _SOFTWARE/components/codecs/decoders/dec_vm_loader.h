#pragma once
/**
 * @file dec_vm_loader.h
 * @brief Header-only decoder table for the "VM Program Load" packet class (0x04).
 *
 * Wire format handled by this class:
 * @code
 *   [0x04] [0xYY] [ payload ]
 *    class  packet
 * @endcode
 *
 * Unlike [[dec_sys_contracts.h]] these packets are not fixed-size structs --
 * 0x42 and 0x43 carry a repeating record list whose entries vary in length --
 * so they are walked with an explicit cursor instead of the
 * convert_to_packet() copy. Every length is checked against the bytes
 * actually present before it is used; a truncated frame raises
 * ERR_VM_LOAD_SHORT_RECORD rather than reading past the buffer.
 *
 * Load sequence (see vm_loader.h for the state rules):
 * @code
 *   0x40 reset      : -
 *   0x41 open       : u16 obj_cnt, u16 acc_cnt, u32 total_size
 *   0x42 add objs   : u8 n, n x { u16 id, u16 item_count, u8 type, u8 flags,
 *                                 u8 name_len, char name[name_len] }
 *   0x43 set data   : u8 n, n x { u16 id, u16 start_idx, u16 byte_len,
 *                                 u8 data[byte_len] }
 *   0x44 add acc    : u8 n, n x { u16 acc_id, u16 root_obj_id, u8 idx_count,
 *                                 u8 idx_len, u8 idx_data[idx_len] }
 *                     idx_data is idx_count records of { u8 kind, payload }:
 *                       LITERAL -> u32 position
 *                       REF     -> u16 accessor id (must already exist)
 *                       NAME    -> u8 len, char name[len]
 * @endcode
 *
 * All multi-byte fields are little-endian, matching the target.
 *
 * `start_idx` is present unconditionally rather than only on multi-element
 * writes: making its presence depend on comparing byte_len against the
 * object's element width would leave the framing dependent on a lookup the
 * parser has to trust, and would make a single non-zero element impossible
 * to address (one element is never "larger than one element").
 *
 * `item_count` is sent instead of a byte size so a client never needs its own
 * copy of the type-width table -- the device already has it.
 *
 * For a VM_OBJ_PTR object the 0x43 data field is a list of u16 child ids, two
 * bytes each; children must already have been created by an earlier 0x42.
 */

#include <stdint.h>
#include <string.h>
#include <sys/cdefs.h>
#include "esp_log.h"
#include "sys_error.h"
#include "vm_loader.h"

#undef OWNER
#define OWNER OWNER_DEC_VM_LOADER

/* VM_LOADER_CLASS_HEADER (0x04) is defined in vm_loader.h, not here: the
   class is 1:1 with the VM component, so the constant lives with that
   component's own public contract -- same split as SYS_ACTIONS_CLASS_HEADER,
   and unlike SYS_CONTRACTS_CLASS_HEADER which spans three components and is
   therefore owned by its codec. */

/** @brief ESP log tag used by every decoder in this table. */
#define DEC_VM_LOADER_TAG "dec_vm_loader"

#define HEADER_packet_vm_reset 0x40
#define HEADER_packet_vm_open 0x41
#define HEADER_packet_vm_add_objs 0x42
#define HEADER_packet_vm_set_data 0x43
#define HEADER_packet_vm_add_acc 0x44

// little-endian readers -- the cursor is a raw byte stream, so nothing here
// may assume the alignment a struct cast would imply
static inline uint16_t dec_vm_u16(const uint8_t* p) {
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static inline uint32_t dec_vm_u32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/**
 * @brief Guard every read: `in_need` more bytes must remain at `in_off` of `in_len`.
 *
 * Parameters are named in_* so plain token substitution cannot collide with
 * the .packet/.need/.got designators below -- same reason SE_CHECK_IN_RANGE
 * in [[sys_error.h]] uses in_val/in_min/in_max.
 */
#define DEC_VM_NEED(in_pkt, in_off, in_len, in_need)                                        \
  do {                                                                                      \
    if ((uint32_t)(in_off) + (uint32_t)(in_need) > (uint32_t)(in_len)) {                    \
      SE_RET_ERR(ERR_VM_LOAD_SHORT_RECORD, .packet = (uint8_t)(in_pkt),                     \
                 .need = (uint16_t)(in_need),                                               \
                 .got = (uint16_t)((in_len) > (in_off) ? (in_len) - (in_off) : 0));         \
    }                                                                                       \
  } while (0)

/** @brief 0x40 -- drop whatever is loaded, leaving the VM fail-closed. */
static inline err_h decoder_packet_vm_reset(void) {
  vm_loader_reset();
  ESP_LOGI(DEC_VM_LOADER_TAG, "storage reset");
  return NULL;
}

/** @brief 0x41 -- reserve the id tables and cap the arena. */
static inline err_h decoder_packet_vm_open(const uint8_t* body, size_t len) {
  DEC_VM_NEED(HEADER_packet_vm_open, 0, len, 8);
  uint16_t obj_cnt = dec_vm_u16(body);
  uint16_t acc_cnt = dec_vm_u16(body + 2);
  uint32_t total = dec_vm_u32(body + 4);
  // logged after the call, so a rejected open cannot read as a successful one
  SE_RET_IF_ERR(vm_loader_open(obj_cnt, acc_cnt, total));
  ESP_LOGI(DEC_VM_LOADER_TAG, "open: %u objects, %u accessors, %lu bytes", obj_cnt, acc_cnt, (unsigned long)total);
  return NULL;
}

/** @brief 0x44 -- create a batch of accessors. */
static inline err_h decoder_packet_vm_add_acc(const uint8_t* body, size_t len) {
  DEC_VM_NEED(HEADER_packet_vm_add_acc, 0, len, 1);
  uint8_t n = body[0];
  size_t off = 1;

  for (uint8_t i = 0; i < n; i++) {
    DEC_VM_NEED(HEADER_packet_vm_add_acc, off, len, 6);
    uint16_t acc_id = dec_vm_u16(body + off);
    uint16_t root_id = dec_vm_u16(body + off + 2);
    uint8_t idx_count = body[off + 4];
    uint8_t idx_len = body[off + 5];
    off += 6;

    DEC_VM_NEED(HEADER_packet_vm_add_acc, off, len, idx_len);
    SE_RET_IF_ERR(vm_loader_add_accessor(acc_id, root_id, idx_count, body + off, idx_len));
    off += idx_len;
  }
  return NULL;
}

/** @brief 0x42 -- create a batch of objects. */
static inline err_h decoder_packet_vm_add_objs(const uint8_t* body, size_t len) {
  DEC_VM_NEED(HEADER_packet_vm_add_objs, 0, len, 1);
  uint8_t n = body[0];
  size_t off = 1;

  for (uint8_t i = 0; i < n; i++) {
    DEC_VM_NEED(HEADER_packet_vm_add_objs, off, len, 7);
    uint16_t id = dec_vm_u16(body + off);
    uint16_t item_count = dec_vm_u16(body + off + 2);
    uint8_t type = body[off + 4];
    uint8_t flags = body[off + 5];
    uint8_t name_len = body[off + 6];
    off += 7;

    DEC_VM_NEED(HEADER_packet_vm_add_objs, off, len, name_len);
    const char* name = name_len ? (const char*)(body + off) : NULL;
    off += name_len;

    SE_RET_IF_ERR(vm_loader_add_obj(id, item_count, type, flags, name, name_len));
  }
  return NULL;
}

/** @brief 0x43 -- fill payload bytes, or link children for a PTR object. */
static inline err_h decoder_packet_vm_set_data(const uint8_t* body, size_t len) {
  DEC_VM_NEED(HEADER_packet_vm_set_data, 0, len, 1);
  uint8_t n = body[0];
  size_t off = 1;

  for (uint8_t i = 0; i < n; i++) {
    DEC_VM_NEED(HEADER_packet_vm_set_data, off, len, 6);
    uint16_t id = dec_vm_u16(body + off);
    uint16_t start_idx = dec_vm_u16(body + off + 2);
    uint16_t byte_len = dec_vm_u16(body + off + 4);
    off += 6;

    DEC_VM_NEED(HEADER_packet_vm_set_data, off, len, byte_len);
    SE_RET_IF_ERR(vm_loader_set_data(id, start_idx, body + off, byte_len));
    off += byte_len;
  }
  return NULL;
}

/**
 * @brief Class handler for VM_LOADER_CLASS_HEADER (0x04).
 *
 * @param data Frame bytes with the class byte already stripped -- data[0] is 0xYY.
 * @param len Number of bytes available at @p data.
 * @return err_h NULL on success, ERR_INTERFACE_UNKNOWN_PACKET for an unmapped
 *               header, or the loader's own error chain.
 */
static inline err_h dec_vm_loader_decode(const uint8_t* data, size_t len) {
  if (len == 0) {
    SE_RET_ERR(ERR_INTERFACE_SHORT_FRAME, .got = 0, .need = 1);
  }

  const uint8_t* body = data + 1;
  size_t body_len = len - 1;

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
    default:
      ESP_LOGW(DEC_VM_LOADER_TAG, "unknown packet header 0x%02X", data[0]);
      SE_RET_ERR(ERR_INTERFACE_UNKNOWN_PACKET, .class_header = VM_LOADER_CLASS_HEADER, .packet_header = data[0]);
  }
}
