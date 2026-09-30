#pragma once
/**
 * @file dec_settings_project.h
 * @brief Stored code packets (class 0x0A): store, check, read back and load
 * the frame list the board replays at boot (SYS_PROJECT.MD).
 *
 * Wire format: [class] [packet] [payload]. Multi-byte values are little-endian.
 */

#include <sdkconfig.h>
#include <stddef.h>
#include <string.h>
#include "dec_sys_actions.h"
#include "dec_vm_loader.h"
#include "sys_error.h"
#include "sys_interface.h"
#include "sys_project.h"

//@settings project @title Stored code @description Store the code the board replays at boot, read it back, set its options and load it.
//@settings-class SETTINGS_PROJECT

#undef OWNER
#define OWNER OWNER_DEC_SETTINGS_PROJECT

#define HEADER_packet_settings_project_begin_t 0x01
typedef struct __packed {
  uint32_t length; //@required @alias Length @unit bytes @description Bytes of the frame list that follows in write packets.
} packet_settings_project_begin_t;

#define HEADER_packet_settings_project_write_t 0x02
typedef struct __packed {
  uint32_t offset; //@required @alias Offset @unit bytes @description Bytes written so far: chunks go in order.
  uint8_t data[];  //@required @alias Data @description The next bytes of the frame list.
} packet_settings_project_write_t;

#define HEADER_packet_settings_project_commit_t 0x03
typedef struct __packed {
  uint32_t crc32;     //@required @alias CRC-32 @description CRC-32 (zlib) of the whole frame list.
  uint32_t schema_id; //@required @alias Schema ID @description Error schema ID of the descriptors the code was built with; must equal the firmware's.
} packet_settings_project_commit_t;

#define HEADER_packet_settings_project_abort_t 0x04
typedef struct __packed {
} packet_settings_project_abort_t;

#define HEADER_packet_settings_project_erase_t 0x05
typedef struct __packed {
} packet_settings_project_erase_t;

#define HEADER_packet_settings_project_options_t 0x06
typedef struct __packed {
  uint8_t autostart; //@required @alias Autostart @description 1: start the VM after the boot replay (when every VM frame applied).
} packet_settings_project_options_t;

#define HEADER_packet_settings_project_info_t 0x07
typedef struct __packed {
} packet_settings_project_info_t;

typedef struct __packed {
  uint8_t stored;               //@alias Code Stored
  uint8_t autostart;            //@alias Autostart
  uint32_t length;              //@alias Length @unit bytes
  uint16_t frame_count;         //@alias Frames
  uint32_t crc32;               //@alias CRC-32
  uint32_t schema_id;           //@alias Stored Schema ID
  uint32_t firmware_schema_id;  //@alias Firmware Schema ID
  uint32_t capacity;            //@alias Capacity @unit bytes
  uint8_t replay_state;         //@alias Last Replay @enum-ref sys_project_replay_e
  uint16_t replay_applied;      //@alias Frames Applied
  uint16_t replay_failed;       //@alias Frames Refused
  uint16_t replay_first_failed; //@alias First Refused Frame @description 65535 when none.
  uint16_t replay_first_tag;    //@alias First Refusal Tag
  uint16_t replay_first_owner;  //@alias First Refusal Owner
  uint32_t replay_crc32;        //@alias Replayed CRC-32 @description CRC-32 of the code this boot replayed (0: none). Differs from crc32 after a store without restart.
} packet_settings_project_info_response_t;

/* The response is the requested bytes themselves (no struct): length ≤ CONFIG_SYS_INTERFACE_RESPONSE_MAX. */
#define HEADER_packet_settings_project_read_t 0x08
typedef struct __packed {
  uint32_t offset; //@required @alias Offset @unit bytes
  uint16_t length; //@required @alias Length @unit bytes @max CONFIG_SYS_INTERFACE_RESPONSE_MAX
} packet_settings_project_read_t;

#define HEADER_packet_settings_project_load_t 0x09
typedef struct __packed {
} packet_settings_project_load_t;

/* What stored code may not contain: this class itself, action recording (a
   recorded action is runtime state of its own) and VM exec (autostart is the
   prj_opts setting). */
static inline bool dec_settings_project_frame_allowed(const uint8_t* frame, size_t len) {
  if (len < 2) return false;
  if (frame[0] == CONFIG_RX_PACKET_CLASS_SETTINGS_PROJECT) return false;
  if (frame[0] == CONFIG_RX_PACKET_CLASS_SYS_ACTIONS &&
      (frame[1] == HEADER_packet_sys_action_record_start_t || frame[1] == HEADER_packet_sys_action_record_stop_t ||
       frame[1] == HEADER_packet_sys_action_remove_t || frame[1] == HEADER_packet_sys_action_remove_all_t)) {
    return false;
  }
  if (frame[0] == CONFIG_RX_PACKET_CLASS_VM_LOADER && frame[1] == HEADER_packet_vm_exec) return false;
  return true;
}

/** A VM loader frame: its refusal at boot cancels autostart. */
static inline bool dec_settings_project_is_vm_frame(const uint8_t* frame, size_t len) {
  return len >= 1 && frame[0] == CONFIG_RX_PACKET_CLASS_VM_LOADER;
}

static inline SE_MUST_USE err_h dec_settings_project_info(void) {
  sys_project_info_t info;
  SE_TRY(sys_project_get_info(&info));
  const packet_settings_project_info_response_t rsp = {
      .stored = info.stored ? 1 : 0,
      .autostart = info.autostart ? 1 : 0,
      .length = info.length,
      .frame_count = info.frame_count,
      .crc32 = info.crc32,
      .schema_id = info.schema_id,
      .firmware_schema_id = SE_schema_id(),
      .capacity = info.capacity,
      .replay_state = (uint8_t)info.report.state,
      .replay_applied = info.report.applied,
      .replay_failed = info.report.failed,
      .replay_first_failed = info.report.first_failed,
      .replay_first_tag = info.report.first_tag,
      .replay_first_owner = info.report.first_owner,
      .replay_crc32 = info.report.crc32,
  };
  return sys_interface_respond(&rsp, sizeof(rsp));
}

static inline SE_MUST_USE err_h dec_settings_project_read(const uint8_t* body, size_t len) {
  packet_settings_project_read_t packet;
  SE_TRY(convert_to_packet(body, len, &packet, sizeof(packet)));
  if (packet.length > CONFIG_SYS_INTERFACE_RESPONSE_MAX) {
    SE_FAIL(ERR_INTERFACE_RESPONSE_TOO_LONG, .got = packet.length, .max = CONFIG_SYS_INTERFACE_RESPONSE_MAX);
  }
  uint8_t out[CONFIG_SYS_INTERFACE_RESPONSE_MAX];
  SE_TRY(sys_project_read(packet.offset, out, packet.length));
  return sys_interface_respond(out, packet.length);
}

static inline SE_MUST_USE err_h dec_settings_project_decode(const uint8_t* data, size_t len) {
  if (len == 0) {
    SE_FAIL(ERR_INTERFACE_SHORT_FRAME, .got = 0, .need = 1);
  }
  const uint8_t* body = data + 1;
  const size_t body_len = len - 1u;

  switch (data[0]) {
    case HEADER_packet_settings_project_begin_t: {
      packet_settings_project_begin_t packet;
      SE_TRY(convert_to_packet(body, body_len, &packet, sizeof(packet)));
      return sys_project_begin(packet.length);
    }
    case HEADER_packet_settings_project_write_t: {
      if (body_len < offsetof(packet_settings_project_write_t, data)) {
        SE_FAIL(ERR_INTERFACE_SHORT_FRAME, .got = (uint32_t)body_len, .need = (uint32_t)offsetof(packet_settings_project_write_t, data));
      }
      uint32_t offset;
      memcpy(&offset, body, sizeof(offset));
      const size_t head = offsetof(packet_settings_project_write_t, data);
      return sys_project_write(offset, body + head, body_len - head);
    }
    case HEADER_packet_settings_project_commit_t: {
      packet_settings_project_commit_t packet;
      SE_TRY(convert_to_packet(body, body_len, &packet, sizeof(packet)));
      return sys_project_commit(packet.crc32, packet.schema_id, dec_settings_project_frame_allowed);
    }
    case HEADER_packet_settings_project_abort_t:
      return sys_project_abort();
    case HEADER_packet_settings_project_erase_t:
      return sys_project_erase();
    case HEADER_packet_settings_project_options_t: {
      packet_settings_project_options_t packet;
      SE_TRY(convert_to_packet(body, body_len, &packet, sizeof(packet)));
      return sys_project_set_options(packet.autostart != 0);
    }
    case HEADER_packet_settings_project_info_t:
      return dec_settings_project_info();
    case HEADER_packet_settings_project_read_t:
      return dec_settings_project_read(body, body_len);
    case HEADER_packet_settings_project_load_t:
      return sys_project_load();
    default:
      SE_FAIL(ERR_INTERFACE_UNKNOWN_PACKET, .class_header = CONFIG_RX_PACKET_CLASS_SETTINGS_PROJECT, .packet_header = data[0]);
  }
}
