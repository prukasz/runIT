#include "sys_device_router.h"
#include "sys_device.h"

// After the root key: [dir u8][id u16][op u16][args].
typedef struct __attribute__((packed)) {
  uint8_t dir;
  uint16_t id;
  uint16_t op;
} hdr_t;

// The command is a sys_device_call: the same code runs for a remote command and for a call from the firmware.
err_h sys_device_router(CMD_F_PARAMS) {
  (void)ctx;
  const uint32_t got = in.len > 0 ? in.len - 1 : 0;  // after the key
  ERR_CHECK(got >= sizeof(hdr_t), ERR_CMD_BAD_LEN, .got = (uint16_t)got, .need = sizeof(hdr_t));
  hdr_t h;
  memcpy(&h, in.data + 1, sizeof(h));  // little-endian target
  return sys_device_call(h.id, h.dir, h.op, in.data + 1 + sizeof(h), got - sizeof(h), out->data, out->cap, &out->len);
}
