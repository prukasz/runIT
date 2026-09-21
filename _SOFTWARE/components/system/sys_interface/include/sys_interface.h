#pragma once
#include "utils.h"
#include "sys_error.h"
#include "sys_data_connector.h"
#include <sdkconfig.h>

#define SYS_INTERFACE_CONNECTOR_ID CONFIG_SYS_DATA_CONN_ID_INTERFACE

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
 *   decoder with sys_interface_register_decoder().
 * * `0xYY` - **packet byte**: interpreted by the class handler alone. Two
 *   classes may reuse the same packet byte for unrelated packets.
 *
 * sys_interface_decode() consumes `0xXX` and forwards the rest of the frame to
 * the registered handler, so a handler always sees `0xYY` at `data[0]`.
 */



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
 * Must be called once at boot, before sys_interface_register_decoder() or
 * sys_interface_register_rx_source(). Class 0x01 (RX_PACKET_CLASS_SYS_CONTRACTS) is wired
 * to the header-only table in `dec_sys_contracts.h`.
 *
 * @return err_h Status report (NULL on success).
 */
err_h sys_interface_init(void);

/**
 * @brief Register a decoder table for a class byte.
 *
 * Classes are only ever appended, never removed - there is no unregister.
 * Register everything at boot, before sys_interface_register_rx_source() starts the
 * RX pump (the registry is a flat array with linear search, not mutex-protected).
 *
 * @param decoder_header Class byte (0xXX) this decoder owns.
 * @param decoder Decoder invoked with the class byte stripped.
 * @param name Human-readable decoder name used in logs (may be NULL).
 * @return err_h NULL on success, ERR_INTERFACE_CLASS_TAKEN if the class byte is
 *               already bound, or ERR_INTERFACE_NO_CLASS_SLOTS if the registry is full.
 *
 * Example - route class 0x02 to the VM:
 * @code
 * SE_ORIGIN_CALL(sys_interface_register_decoder(0x02, vm_decode, "vm"));
 * @endcode
 */
err_h sys_interface_register_decoder(uint8_t decoder_header, sys_interface_handler_f decoder, const char* name);

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
 * @brief Start capturing live frames in the static tap buffer.
 *
 * Capturing is off by default; while it is off, the RX receiver does not copy
 * frames into the tap buffer.
 */
void sys_interface_tap_capture_start(void);

/**
 * @brief Stop capturing live frames in the static tap buffer.
 *
 * Frames already captured remain available to sys_interface_tap_poll().
 */
void sys_interface_tap_capture_end(void);

/**
 * @brief Pull one frame from the static tap buffer.
 *
 * While capture is active, every live frame decoded by the RX receiver task
 * (class byte included, before dispatch) is copied into this statically
 * allocated ring buffer. Its capacity is CONFIG_SYS_ACTIONS_TAP_BUFFER_SIZE.
 * Frames replayed via a direct sys_interface_decode() call are never tapped.
 *
 * Non-blocking. Call in a loop (from your own task) until *out_len == 0.
 *
 * @param buf Destination buffer.
 * @param max_len Capacity of @p buf - a longer frame is truncated.
 * @param out_len Set to the popped frame's length, or 0 if nothing is
 *                pending.
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
 * own capacity. If the connector's wake semaphore was actually given while
 * suspended, the receiver waits 1ms and gives it back before looping, so
 * resume notices the still-pending data within ~1ms instead of waiting for
 * the next full poll tick.
 *
 * Used by `sys_actions_invoke()` so a replayed packet sequence can't interleave
 * with live incoming traffic - see [[SYS_ACTIONS.MD]].
 */
void sys_interface_suspend_rx(void);
void sys_interface_resume_rx(void);
bool sys_interface_is_rx_suspended(void);

/**
 * @brief Transmit data over the system interface data connector.
 *
 * Dispatches @p data over SYS_INTERFACE_CONNECTOR_ID via sys_data_connector_send().
 *
 * @param data Outbound payload buffer.
 * @param len Length in bytes.
 * @return err_h NULL on success, ERR_NULL_PTR if data is NULL, or ERR_BASE_NOT_FOUND if connector unallocated.
 */
err_h sys_interface_send(const void* data, size_t len);
