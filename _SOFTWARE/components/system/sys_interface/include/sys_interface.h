#pragma once
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
 * @brief Non-invasive frame observer.
 *
 * Called with every complete frame sys_interface_decode() receives (class byte
 * included, before dispatch) - purely an observer, it cannot affect decode's
 * return value or control flow, and only one tap can be registered at a time
 * (a later call to sys_interface_set_tap() replaces the previous one). Used by
 * `sys_actions` to record live traffic; see [[SYS_ACTIONS.MD]].
 *
 * @param frame Full frame bytes, class byte at frame[0].
 * @param len Total frame length.
 */
typedef void (*sys_interface_tap_f)(const uint8_t* frame, size_t len);

/**
 * @brief Register (or clear, with NULL) the frame tap. See sys_interface_tap_f.
 */
void sys_interface_set_tap(sys_interface_tap_f tap);

/**
 * @brief Suspend/resume the RX pump's dispatch of newly drained frames.
 *
 * Nesting-safe via a depth counter, the same shape as SE_suspend()/SE_resume().
 * This is a best-effort signal, not a hard synchronization barrier: while
 * suspended, sys_interface_bind_ble_rx()'s worker task still drains each
 * characteristic's RX buffer (so it can't overflow) but drops every frame
 * instead of decoding it, logging a warning per drop. It does not wait for an
 * in-flight sys_interface_decode() call to finish before returning.
 *
 * Used by `sys_actions_invoke()` so a replayed packet sequence can't interleave
 * with live incoming traffic - see [[SYS_ACTIONS.MD]].
 */
void sys_interface_suspend_rx(void);
void sys_interface_resume_rx(void);
bool sys_interface_is_rx_suspended(void);

/**
 * @brief Attach a BLE characteristic's RX buffer as the frame source.
 *
 * Starts the single static worker task (there is only one RX pump - a second
 * call fails with ERR_BASE_INVALID_STATE) that blocks on the characteristic's
 * RX semaphore, drains every queued peer write (one buffer item is one whole
 * frame, never split or coalesced) and feeds each one to sys_interface_decode().
 * Decoder errors are emitted through SE_ORIGIN_CALL() so a bad frame never
 * stops the pump.
 *
 * @param char_uuid 16-bit UUID of the characteristic to drain (must have been
 *                  created with a non-zero rx_buffer_size).
 * @param max_frame_len Largest frame accepted, up to SYS_INTERFACE_RX_FRAME_CAP
 *                       (512); longer items are truncated by the buffer.
 * @return err_h NULL on success, ERR_INVALID_VAL_UI32 if max_frame_len is 0 or
 *               exceeds the cap, ERR_BASE_INVALID_STATE if already bound or the
 *               characteristic has no RX buffer, ERR_BASE_NO_MEM if the task
 *               failed to start, or the sys_ble lookup error.
 *
 * Example:
 * @code
 * SE_ORIGIN_CALL(sys_interface_init());
 * SE_ORIGIN_CALL(sys_interface_bind_ble_rx(SYS_BLE_CHR_RUNIT_RX, 512));
 * @endcode
 */
err_h sys_interface_bind_ble_rx(uint16_t char_uuid, size_t max_frame_len);
