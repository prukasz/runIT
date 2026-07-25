#include "sys_interface.h"
#include "dec_sys.h"
#include "sys_error.h"

#undef OWNER
#define OWNER OWNER_SYS_INTERFACE_DECODE

err_h convert_to_packet(const uint8_t* data, size_t len, void* packet, size_t packet_size) {
  if (len < packet_size) {
    SE_RET_ERR(ERR_INVALID_VAL_UI32, (uint32_t)len, (uint32_t)packet_size, UINT32_MAX);
  }
  memcpy(packet, data, packet_size);
  return NULL;
}

#define DECODE_CASE(header, packet_type, decoder_func)                                     \
  case header: {                                                                           \
    packet_type packet;                                                                    \
    err_h err = convert_to_packet(data + 1, len - 1, &packet, sizeof(packet_type)); \
    if (SE_IS_ERR(err)) return err;                                                           \
    return decoder_func(&packet);                                                          \
  }

err_h sys_interface_decode(uint8_t* data, size_t len) {
  if (len == 0) {
    SE_RET_ERR(ERR_INVALID_VAL_UI32, 0, 1, UINT32_MAX);
  }

  switch (data[0]) {
    SYS_PACKET_LIST(DECODE_CASE)
    default:
      SE_RET_ERR(ERR_BASE_NOT_FOUND, data[0]);
  }
}
