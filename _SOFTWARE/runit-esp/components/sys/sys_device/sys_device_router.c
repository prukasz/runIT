#include "sys_device_router.h"
#include "sys_device.h"

// After the root key: [id u16][op u16][args].
typedef struct __attribute__((packed)) {
  uint16_t id;
  uint16_t op;
} hdr_t;

// The command is a sys_device_call: the same code runs for a remote command and for a call from the firmware.
err_h sys_device_router(void* ctx, const uint8_t* in, uint32_t in_len, uint8_t* out, uint32_t out_cap,
                        uint32_t* out_len) {
  (void)ctx;
  const uint32_t got = in_len > 0 ? in_len - 1 : 0;  // after the key
  ERR_CHECK(got >= sizeof(hdr_t), ERR_CMD_BAD_LEN, .got = (uint16_t)got, .need = sizeof(hdr_t));
  hdr_t h;
  memcpy(&h, in + 1, sizeof(h));  // little-endian target
  return sys_device_call(h.id, h.op, in + 1 + sizeof(h), got - sizeof(h), out, out_cap, out_len);
}
