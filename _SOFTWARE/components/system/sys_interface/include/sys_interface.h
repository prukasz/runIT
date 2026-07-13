#pragma once
#include <stdint.h>
#include <string.h>
#include "status.h"


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
 * @return status_rep_t Status report (STA_OK on success, or ERR_INVALID_SIZE status).
 */
status_rep_t convert_to_packet(const uint8_t* data, size_t len, void* packet, size_t packet_size);

/**
 * @brief Global decoder interface that dispatches incoming packets based on the header byte.
 *
 * Uses X-macro expansion of the SYS_PACKET_LIST to match data[0] to its registered
 * packet type and decoder callback.
 *
 * @param data Pointer to raw incoming BLE packet payload (header at data[0]).
 * @param len Total length of raw packet payload.
 * @return status_rep_t Status report of the executed decoder function.
 */
status_rep_t sys_interface_decode(uint8_t* data, size_t len);

// No-op definition for file-scope declarations in header files
#define SYS_DEFINE_PACKET(header, packet_type, decoder_func)
