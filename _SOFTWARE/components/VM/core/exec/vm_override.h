#pragma once
#include <sdkconfig.h>
#include "sys_error.h"

typedef struct __attribute__((packed)) {
  uint16_t id;
  uint16_t start_idx;
  uint16_t len;
  uint8_t data[];
} vm_override_record_t;

/**
 * @brief Enqueue a runtime variable update record (called from Core 0 decoder).
 * Validates object existence, scalar/array type, user protection, mutability,
 * and bounds before enqueueing. VM_OBJ_PTR is rejected: pointer relinking must
 * resolve child IDs through the loader/link API rather than copy wire bytes
 * into native pointer slots.
 * Supports arbitrary variable-length scalar/array data up to buffer capacity.
 */
SE_MUST_USE err_h vm_override_post(uint16_t id, uint16_t start_idx, const uint8_t* data, uint16_t len);

/**
 * @brief Drain and apply all pending runtime variable updates (called from Core 1 at scan boundary).
 * Revalidates the queued record and target, then copies data to object payload
 * and sets upd = 1 so downstream blocks and telemetry react. Malformed or stale
 * records are reported and discarded without modifying an object.
 */
void vm_override_drain(void);

/**
 * @brief Reset the override queue.
 */
void vm_override_reset(void);
