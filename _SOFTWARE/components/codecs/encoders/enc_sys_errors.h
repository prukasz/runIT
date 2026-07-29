#pragma once
/**
 * @file enc_sys_errors.h
 * @brief Header-only encoder that flattens an `err_h` error chain into a single
 *        self-describing byte packet, so the client can rebuild the whole stack
 *        trace (and log it there) instead of the firmware spending flash on
 *        human-readable strings.
 *
 * Mirror image of `decoders/dec_sys_contracts.h`: the decoder side turns wire
 * bytes into `sys_*` calls, this side turns an in-RAM structure into wire bytes.
 * It only *fills a buffer* — pushing that buffer into a TX ring
 * (`sys_ble_char_send()`) is the caller's job.
 *
 * ## Wire format
 *
 * There is **no class byte**. The outbound stream is already identified by the
 * TX slot header byte that `sys_buff_pop_framed()` prepends (see
 * `sys_ble_char_assign_tx_buffer()`), so adding another one here would be
 * redundant.
 *
 * @code
 *   +--------- packet header (3 B) ---------+
 *   | u8 version | u8 node_count | u8 depth |
 *   +---------------------------------------+
 *   | node[0] | node[1] | ... | node[n-1]   |   node_count records, back-to-back
 *   +---------------------------------------+
 *
 *   node record (5 B header + payload):
 *   +----------------+------------------+------------------+-------------+
 *   | u8 payload_len | u16 tag (LE)     | u16 owner (LE)   | payload ... |
 *   +----------------+------------------+------------------+-------------+
 * @endcode
 *
 * | Field | Meaning |
 * | :--- | :--- |
 * | `version` | `ENC_SYS_ERRORS_FMT_VERSION` — bumped on any layout change. |
 * | `node_count` | Node records actually present in this packet. |
 * | `depth` | Full length of the chain that was walked. `depth > node_count` means the packet was truncated (buffer full, or the chain hit `ENC_SYS_ERRORS_MAX_NODES`). |
 * | `payload_len` | `sizeof(err_payload_<TAG>_t)` — repeated on the wire so a client with a stale tag table can still skip an unknown node. |
 * | `tag` | `err_tag_e` value. |
 * | `owner` | `sys_owner_e` value. |
 * | `payload` | The node's payload struct copied verbatim. |
 *
 * Nodes are emitted **outermost first** (the `SE_ORIGIN_CALL` frame), each
 * following record being the previous one's `next_cause`. The `next_cause`
 * pointer itself is stripped — ordering carries it — and an explicit index byte
 * is dropped for the same reason: records are contiguous and `node_count`
 * bounds them, so an index would only ever restate the loop counter.
 *
 * ## Client side (Python `ctypes`)
 *
 * Payload structs are **not** `__packed` — they are plain C structs, so mirror
 * them with a default-alignment `ctypes.Structure` (not `_pack_ = 1`). The
 * three fixed-size fields of a node record are little-endian and unaligned:
 * `struct.unpack_from("<BHH", buf, off)`.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "sys_error.h"

#undef OWNER
#define OWNER OWNER_ENC_SYS_ERRORS

/** @brief Wire format revision — bump whenever the layout below changes. */
#define ENC_SYS_ERRORS_FMT_VERSION 0x01

/** @brief Fixed packet header: version + node_count + depth. */
#define ENC_SYS_ERRORS_HDR_LEN 3u

/** @brief Per-node fixed header: payload_len + tag(u16) + owner(u16). */
#define ENC_SYS_ERRORS_NODE_HDR_LEN 5u

/**
 * @brief Hard stop on chain traversal.
 *
 * Error nodes live in a lock-free ring with no free (see SYS_ERRORS.MD), so a
 * chain held for too long can have a `next_cause` overwritten into a cycle.
 * This cap guarantees the walk always terminates.
 */
#define ENC_SYS_ERRORS_MAX_NODES 16u

/** @brief Smallest buffer that can hold the header plus one zero-payload node. */
#define ENC_SYS_ERRORS_MIN_BUF (ENC_SYS_ERRORS_HDR_LEN + ENC_SYS_ERRORS_NODE_HDR_LEN)

/**
 * @brief Flatten an error chain into a ready-to-send packet.
 *
 * Walks @p chain from the outermost node down its `next_cause` links and writes
 * one record per node into @p out_buf. Stops early — without failing — when the
 * next record would not fit or `ENC_SYS_ERRORS_MAX_NODES` is reached; the
 * `depth` byte then exceeds `node_count` so the client knows the trace is
 * partial.
 *
 * @note Every error this function can raise is raised *before* the chain is
 * walked (except the "not even one node fit" case, checked after). That matters:
 * `SE_*` macros allocate from the same ring the chain lives in, so allocating
 * mid-walk could clobber the very nodes being encoded.
 *
 * @param chain Error chain to encode (must not be NULL).
 * @param out_buf Destination buffer.
 * @param out_max Capacity of @p out_buf, at least ENC_SYS_ERRORS_MIN_BUF.
 * @param out_len Receives the final packet length.
 * @return err_h NULL on success, ERR_NULL_PTR for a NULL argument, or
 *               ERR_INTERFACE_ENC_BUF_TOO_SMALL (carrying got/need) if the
 *               buffer cannot hold the header plus the first node.
 *
 * Example — encode and hand off to a TX ring:
 * @code
 * uint8_t pkt[128];
 * size_t  pkt_len = 0;
 * if (SE_IS_OK(enc_sys_errors_encode_chain(chain, pkt, sizeof(pkt), &pkt_len))) {
 *   sys_ble_char_send(SYS_BLE_CHR_RUNIT_LOGS, PACKET_HEADER_ERRORS, pkt, pkt_len, true);
 * }
 * @endcode
 */
static inline err_h enc_sys_errors_encode_chain(err_h chain, uint8_t* out_buf, size_t out_max, size_t* out_len) {
  SE_CHECK_NOT_NULL(chain);
  SE_CHECK_NOT_NULL(out_buf);
  SE_CHECK_NOT_NULL(out_len);
  if (out_max < ENC_SYS_ERRORS_MIN_BUF) {
    SE_RET_ERR(ERR_INTERFACE_ENC_BUF_TOO_SMALL, .got = (uint32_t)out_max, .need = ENC_SYS_ERRORS_MIN_BUF);
  }

  size_t pos = ENC_SYS_ERRORS_HDR_LEN;
  uint8_t node_count = 0;
  uint8_t depth = 0;
  bool full = false;

  for (err_h node = chain; node != NULL && depth < ENC_SYS_ERRORS_MAX_NODES; node = node->next_cause) {
    depth++;
    if (full) continue;  // keep counting depth so the client sees how much it lost

    size_t payload_len = SE_get_payload_size(node->tag);
    if (payload_len > UINT8_MAX) payload_len = UINT8_MAX;  // unreachable: the ring's _Static_assert caps payloads well below this
    if (pos + ENC_SYS_ERRORS_NODE_HDR_LEN + payload_len > out_max) {
      full = true;
      continue;
    }

    out_buf[pos++] = (uint8_t)payload_len;
    out_buf[pos++] = (uint8_t)((uint32_t)node->tag & 0xFFu);
    out_buf[pos++] = (uint8_t)(((uint32_t)node->tag >> 8) & 0xFFu);
    out_buf[pos++] = (uint8_t)(node->owner & 0xFFu);
    out_buf[pos++] = (uint8_t)((node->owner >> 8) & 0xFFu);
    if (payload_len > 0) {
      memcpy(&out_buf[pos], node->payload, payload_len);
      pos += payload_len;
    }
    node_count++;
  }

  if (node_count == 0) {
    SE_RET_ERR(ERR_INTERFACE_ENC_BUF_TOO_SMALL, .got = (uint32_t)out_max,
               .need = (uint32_t)(ENC_SYS_ERRORS_HDR_LEN + ENC_SYS_ERRORS_NODE_HDR_LEN + SE_get_payload_size(chain->tag)));
  }

  out_buf[0] = ENC_SYS_ERRORS_FMT_VERSION;
  out_buf[1] = node_count;
  out_buf[2] = depth;
  *out_len = pos;
  return NULL;
}
