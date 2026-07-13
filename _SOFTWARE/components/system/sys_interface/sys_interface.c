#include "sys_interface.h"
#include "dec_sys.h"
#include "status.h"

status_rep_t convert_to_packet(const uint8_t* data, size_t len, void* packet, size_t packet_size) {
  if (len < packet_size) {
    return STA_C(ERR_INVALID_SIZE, OWNER_SYS_INTERFACE_DECODE, len, STATUS_PAYLOAD_UNKNOWN);
  }
  memcpy(packet, data, packet_size);
  return STA_OK;
}

#define DECODE_CASE(header, packet_type, decoder_func)                                     \
  case header: {                                                                           \
    packet_type packet;                                                                    \
    status_rep_t err = convert_to_packet(data + 1, len - 1, &packet, sizeof(packet_type)); \
    if (err.e_code != 0) return err;                                                       \
    return decoder_func(&packet);                                                          \
  }

status_rep_t sys_interface_decode(uint8_t* data, size_t len) {
  if (len == 0) {
    return STA_C(ERR_INVALID_SIZE, OWNER_SYS_INTERFACE_DECODE, len, STATUS_PAYLOAD_UNKNOWN);
  }

  switch (data[0]) {
    SYS_PACKET_LIST(DECODE_CASE)
    default:
      return STA_C(ERR_NOT_FOUND, OWNER_SYS_INTERFACE_DECODE, data[0], STATUS_PAYLOAD_UNKNOWN);
  }
}

