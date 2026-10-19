#pragma once
#include <sdkconfig.h>
#include "errors.h"
#include "vm_obj.h"

/**
 * @file vm_sub.h
 * @brief VM object subscription and telemetry mechanism.
 *
 * A client subscribes to a list of object IDs (VM_OP_SUBSCRIBE). After
 * every completed pass, vm_sub_scan() walks each subscribed object and its PTR
 * children (8 levels) and sends, on the telemetry connector, every object whose
 * value differs from what was last sent -- whether or not the block that wrote
 * it marked it fresh (a quiet write is a change too):
 *
 *   - value records (VM_OP_SET_DATA, vm_wire_data_t):
 *     [u16 id][u16 start_idx][u16 byte_len][data]; a PTR's data is its child
 *     IDs (u16 each); an object larger than a frame is split, start_idx counts
 *     elements.
 *   - describe records (VM_OP_ADD_OBJECTS, vm_wire_obj_t) for heap
 *     objects (ID has VM_OBJ_ID_DYN_BIT), which the app never uploaded:
 *     [u16 id][vm_wire_head_t][name]. Sent before any value that may reference
 *     them, when first seen and whenever type, size or name change.
 *
 * A new subscription starts with a full snapshot: at the end of the next pass,
 * or at once if the VM is stopped.
 */

/**
 * @brief Initialize subscription subsystem and hook into vm_exec sample point.
 * @return err_h NULL on success.
 */
MUST_USE err_h vm_sub_init(void);

/**
 * @brief Set the list of subscribed object IDs.
 *
 * Replaces the previous list. Everything reachable is sent in full at the end
 * of the next pass, or right away (under the program barrier) if the VM is
 * stopped. Safe to call from any task except the VM task.
 *
 * @param ids Array of object IDs to subscribe to (VM_OBJ_ID_DYN_BIT for heap objects).
 * @param count Number of IDs in the array. 0 clears all subscriptions.
 * @return err_h NULL on success; ERR_VM_BAD_ARG if count exceeds
 *         CONFIG_VM_SUB_MAX_SUBSCRIBERS (nothing changes).
 */
MUST_USE err_h vm_sub_subscribe(const uint16_t* ids, uint16_t count);

/**
 * @brief Process the payload of VM_OP_SUBSCRIBE (after the root key and the op).
 *
 * Wire format:
 *   [0]     u8 count
 *   [1..]   count * u16 obj_id (little-endian)
 *
 * @param body Pointer to the payload.
 * @param len Length of the payload in bytes.
 * @return err_h NULL on success, or error code.
 */
MUST_USE err_h vm_sub_handle_packet(const uint8_t* body, size_t len);

/**
 * @brief Take over a new subscription list, then send what changed since the
 *        last sample (ADD_OBJECTS / SET_DATA frames). Called by vm_exec's sample hook at
 *        the end of every completed pass; must not run concurrently with a pass.
 */
void vm_sub_scan(void);

/**
 * @brief Clear all active subscriptions. Called by the loader under the program barrier.
 */
void vm_sub_reset(void);

/**
 * @brief Returns the number of currently subscribed objects.
 */
uint16_t vm_sub_count(void);

/**
 * @brief Inspect the currently subscribed IDs.
 *
 * @param out_count Optional pointer to receive count.
 * @return Pointer to internal array of subscribed IDs.
 */
const uint16_t* vm_sub_get_ids(uint16_t* out_count);
