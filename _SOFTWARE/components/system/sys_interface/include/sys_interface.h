#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "sys_error.h"

/**
 * @file sys_interface.h
 * @brief Two-level packet router for every inbound control frame.
 *
 * Wire format:
 * @code
 *   [0xXX] [0xYY] [ payload ... ]
 *    class  packet
 * @endcode
 *
 * * `0xXX` - **class byte**: selects which decoder table owns the frame. The
 *   system contracts table (`dec_sys_contracts.h`) is class `0x01`; further
 *   classes (VM bytecode, callback/device creation, ...) register their own
 *   handler with sys_interface_register_class().
 * * `0xYY` - **packet byte**: interpreted by the class handler alone. Two
 *   classes may reuse the same packet byte for unrelated packets.
 *
 * sys_interface_decode() consumes `0xXX` and forwards the rest of the frame to
 * the registered handler, so a handler always sees `0xYY` at `data[0]`.
 */

/** @brief Maximum number of class handlers registrable at once. */
#define SYS_INTERFACE_MAX_CLASSES 8

/** @brief Maximum number of RX frame sources registrable at once (see sys_interface_register_rx_source()). */
#define SYS_INTERFACE_MAX_RX_SOURCES 4

/** @brief Largest frame any registered RX source can produce. */
#define SYS_INTERFACE_RX_FRAME_CAP 512

/**
 * @brief Class handler signature.
 *
 * @param data Frame bytes with the class byte stripped - data[0] is the packet byte.
 * @param len Number of bytes available at @p data.
 * @return err_h NULL on success, or an error chain describing the failure.
 */
typedef err_h (*sys_interface_handler_f)(const uint8_t* data, size_t len);

/**
 * @brief Helper function to convert raw byte array into a structured packet payload.
 *
 * Checks that the received length is sufficient to cover the packet struct size
 * and copies the data into the packet struct destination.
 *
 * @param data Pointer to input data payload.
 * @param len Length of the data payload.
 * @param packet Destination buffer to copy the structured packet to.
 * @param packet_size Size of the target packet structure.
 * @return err_h NULL on success, or ERR_INTERFACE_SHORT_FRAME if @p len is too small.
 */
err_h convert_to_packet(const uint8_t* data, size_t len, void* packet, size_t packet_size);

/**
 * @brief Reset the class registry and register the built-in system contracts class.
 *
 * Must be called once at boot, before sys_interface_register_class() or
 * sys_interface_bind_ble_rx(). Class 0x01 (SYS_CONTRACTS_CLASS_HEADER) is wired
 * to the header-only table in `dec_sys_contracts.h`.
 *
 * @return err_h Status report (NULL on success).
 */
err_h sys_interface_init(void);

/**
 * @brief Bind a decoder table to a class byte.
 *
 * Classes are only ever appended, never removed - there is no unregister.
 * Register everything at boot, before sys_interface_bind_ble_rx() starts the
 * RX pump (the registry is a flat array with linear search, not mutex-protected).
 *
 * @param class_header Class byte (0xXX) this handler owns.
 * @param handler Handler invoked with the class byte stripped.
 * @param name Human-readable class name used in logs (may be NULL).
 * @return err_h NULL on success, ERR_INTERFACE_CLASS_TAKEN if the class byte is
 *               already bound, or ERR_INTERFACE_NO_CLASS_SLOTS if the registry is full.
 *
 * Example - route class 0x02 to the VM:
 * @code
 * SE_ORIGIN_CALL(sys_interface_register_class(0x02, vm_decode, "vm"));
 * @endcode
 */
err_h sys_interface_register_class(uint8_t class_header, sys_interface_handler_f handler, const char* name);

/**
 * @brief Route one complete frame to the handler registered for its class byte.
 *
 * @param data Pointer to the raw frame (class byte at data[0]).
 * @param len Total length of the frame.
 * @return err_h NULL on success, ERR_INTERFACE_SHORT_FRAME for an empty frame,
 *               ERR_INTERFACE_UNKNOWN_CLASS for an unregistered class byte, or
 *               the class handler's own error chain.
 */
err_h sys_interface_decode(const uint8_t* data, size_t len);

/**
 * @brief Enable the frame tap: every live frame the RX receiver task decodes
 * (class byte included, before dispatch) is also pushed into a small internal
 * ring buffer for a consumer to pull at its own pace via sys_interface_tap_poll().
 *
 * Unlike the old push-callback design, the tap never runs consumer code
 * inline inside the receiver task - it only ever copies bytes into a buffer,
 * so a slow or blocking consumer can't stall RX. Only one tap buffer exists;
 * calling this again while already enabled is a no-op. Frames replayed via
 * sys_actions_invoke() (or any other direct sys_interface_decode() call that
 * doesn't go through the receiver task) are never tapped - see [[SYS_ACTIONS.MD]].
 *
 * @param buf_size Ring buffer capacity in bytes.
 * @return err_h NULL on success, or ERR_BASE_NO_MEM.
 */
err_h sys_interface_tap_enable(size_t buf_size);

/** @brief Disable the tap and free its buffer. No-op if not enabled. */
void sys_interface_tap_disable(void);

/**
 * @brief Pull one tapped frame, if any is pending.
 *
 * Non-blocking. Call in a loop (from your own task) until *out_len == 0.
 *
 * @param buf Destination buffer.
 * @param max_len Capacity of @p buf - a longer frame is truncated.
 * @param out_len Set to the popped frame's length, or 0 if nothing is
 *                pending (including when the tap isn't enabled).
 * @return err_h Status report (NULL on success).
 */
err_h sys_interface_tap_poll(uint8_t* buf, size_t max_len, size_t* out_len);

/**
 * @brief Suspend/resume the RX receiver's dispatch of newly drained frames.
 *
 * Nesting-safe via a depth counter, the same shape as SE_suspend()/SE_resume().
 * This is a best-effort signal, not a hard synchronization barrier: while
 * suspended, the receiver task does not drain any registered source at all -
 * frames simply accumulate in each source's own buffer (e.g. a BLE
 * characteristic's rx_buff) rather than being dropped, up to that buffer's
 * own capacity. If the wake semaphore (sys_interface_get_rx_wake_sem()) was
 * actually given while suspended, the receiver waits 1ms and gives it back
 * before looping, so resume notices the still-pending data within ~1ms
 * instead of waiting for the next full poll tick.
 *
 * Used by `sys_actions_invoke()` so a replayed packet sequence can't interleave
 * with live incoming traffic - see [[SYS_ACTIONS.MD]].
 */
void sys_interface_suspend_rx(void);
void sys_interface_resume_rx(void);
bool sys_interface_is_rx_suspended(void);

/**
 * @brief Non-blocking drain callback for a source registered with
 * sys_interface_register_rx_source().
 *
 * Called repeatedly by the RX receiver task until it reports nothing
 * pending, so it must never block. Pop at most one whole frame into @p buf
 * (up to @p max_len bytes) and report its length via @p out_len.
 *
 * @param ctx Opaque context, passed through unchanged from registration.
 * @param buf Destination buffer, at least @p max_len bytes.
 * @param max_len Capacity of @p buf.
 * @param out_len Set to the popped frame's length, or 0 if nothing is pending.
 * @return err_h NULL if @p out_len was set (even to 0 for "nothing pending"),
 *               or an error chain to abort this source's drain for the
 *               current tick (logged, not propagated - the receiver moves on
 *               to the next source and tries again next tick).
 */
typedef err_h (*sys_interface_rx_dequeue_f)(void* ctx, uint8_t* buf, size_t max_len, size_t* out_len);

/**
 * @brief Get the RX receiver's shared wake semaphore.
 *
 * A binary semaphore, owned by sys_interface (not any one transport):
 * any producer that wants the receiver to wake immediately instead of
 * waiting for its next poll tick gives this handle when it has data (e.g.
 * pass it as sys_ble_char_create_t.rx_notify_sem). The receiver always does
 * a full scan of every registered source on each wake regardless of which
 * producer gave it, so there is no per-source bit or identity to assign -
 * any number of producers can share this one handle.
 *
 * @return SemaphoreHandle_t The shared semaphore (always valid - constructed at load time).
 */
SemaphoreHandle_t sys_interface_get_rx_wake_sem(void);

/**
 * @brief Register a frame source with the shared RX receiver.
 *
 * The receiver is a single static task (started lazily on the first
 * successful registration - never more than one, regardless of how many
 * sources are registered) that drains every registered source's queued
 * frames (dequeue_fn called until it reports nothing pending) and feeds each
 * to sys_interface_decode() via SE_ORIGIN_CALL() - a bad frame never stops
 * it. Up to SYS_INTERFACE_MAX_RX_SOURCES may be registered, and registration
 * may happen any time after sys_interface_init(), including well after boot
 * - e.g. when a WiFi or LoRa driver comes up. There is no unregister.
 *
 * The receiver blocks on sys_interface_get_rx_wake_sem() with a
 * SYS_INTERFACE_RX_WAIT_MS (100ms) timeout, so it wakes near-instantly for
 * any source whose producer gives that semaphore, and is otherwise
 * re-checked every tick regardless - which is what catches a source whose
 * producer never gives the semaphore at all (pure polling).
 *
 * @param dequeue_fn Non-blocking drain callback, see sys_interface_rx_dequeue_f.
 * @param ctx Opaque context passed back to dequeue_fn on every call.
 * @param max_frame_len Largest frame this source can produce, up to SYS_INTERFACE_RX_FRAME_CAP.
 * @param name Used in logs only, may be NULL.
 * @return err_h NULL on success, ERR_NULL_PTR if dequeue_fn is NULL,
 *               ERR_INVALID_VAL_UI32 if max_frame_len is out of range,
 *               ERR_INTERFACE_NO_SOURCE_SLOTS if the registry is full, or
 *               ERR_BASE_NO_MEM if the receiver task failed to start (first
 *               registration only).
 *
 * Example - a hypothetical LoRa driver feeding the same router as BLE:
 * @code
 * SE_ORIGIN_CALL(sys_interface_register_rx_source(lora_rx_dequeue, lora_ctx, 256, "lora"));
 * @endcode
 */
err_h sys_interface_register_rx_source(sys_interface_rx_dequeue_f dequeue_fn, void* ctx, size_t max_frame_len, const char* name);

/**
 * @brief Attach a BLE characteristic's RX buffer as a frame source.
 *
 * Thin convenience wrapper around sys_interface_register_rx_source() - see
 * its docs for the shared receiver's behavior. May be called more than once
 * (for different characteristics), up to SYS_INTERFACE_MAX_RX_SOURCES total
 * across every registered source, BLE or otherwise.
 *
 * @param char_uuid 16-bit UUID of the characteristic to drain (must have been
 *                  created with a non-zero rx_buffer_size). To get a
 *                  near-instant wake for this source, create it with
 *                  rx_notify_sem = sys_interface_get_rx_wake_sem().
 * @param max_frame_len Largest frame accepted, up to SYS_INTERFACE_RX_FRAME_CAP
 *                       (512); longer items are truncated by the buffer.
 * @return err_h NULL on success, ERR_BASE_INVALID_STATE if the characteristic
 *               has no RX buffer, the sys_ble lookup error, or any error from
 *               sys_interface_register_rx_source().
 *
 * Example:
 * @code
 * SE_ORIGIN_CALL(sys_interface_init());
 * SE_ORIGIN_CALL(sys_interface_bind_ble_rx(SYS_BLE_CHR_RUNIT_RX, 512));
 * @endcode
 */
err_h sys_interface_bind_ble_rx(uint16_t char_uuid, size_t max_frame_len);
