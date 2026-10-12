#include "sys_device_router.h"
#include "sys_device.h"

//@dispatch device After the key: [device id u16][op u16][args]; the ops are the device_contracts of the catalog
// After the root key: [id u16][op u16][args].
#define IN_ID 0u
#define IN_OP 2u
#define IN_ARGS 4u

static uint16_t u16_at(const uint8_t* b) {
  return (uint16_t)(b[0] | (b[1] << 8));
}

// The command is a sys_device_call: the same code runs for a remote command and for a call from the firmware.
err_h sys_device_router(void* ctx, const uint8_t* in, uint32_t in_len, uint8_t* out, uint32_t out_cap,
                        uint32_t* out_len) {
  (void)ctx;
  const uint32_t got = in_len > 0 ? in_len - 1 : 0;  // after the key
  ERR_CHECK(got >= IN_ARGS, ERR_CMD_BAD_LEN, .got = (uint16_t)got, .need = IN_ARGS);  // [id u16][op u16]
  const uint8_t* cmd = in + 1;
  return sys_device_call(u16_at(cmd + IN_ID), u16_at(cmd + IN_OP), cmd + IN_ARGS, got - IN_ARGS, out, out_cap,
                         out_len);
}
