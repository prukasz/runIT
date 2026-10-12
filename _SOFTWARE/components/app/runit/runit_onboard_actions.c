#include "runit_onboard_actions.h"
#include "router_ids.h"
#include "sdkconfig.h"
#include "sys_actions.h"
#include "sys_device.h"

// Image of one action: [u16 len LE][keys + payload] per frame (SYS_ACTIONS.MD). Here one device-router frame
// [ROUTER_KEY_DEVICE][SYS_DEVICE_ID_NONE u16][op u16]: the op for every device.
#define U16(v) (uint8_t)(v), (uint8_t)((v) >> 8)
#define SWEEP_FRAME(op) 5, 0, ROUTER_KEY_DEVICE, U16(SYS_DEVICE_ID_NONE), U16(op)

static const uint8_t k_suspend_all[] = {SWEEP_FRAME(SYS_DEVICE_OP_SUSPEND)};
static const uint8_t k_resume_all[] = {SWEEP_FRAME(SYS_DEVICE_OP_RESUME)};

err_h runit_onboard_actions_init(void) {
  ERR_TRY(sys_actions_set(SYS_ACTIONS_ID(SYS_ACTIONS_CLASS_USER, CONFIG_RUNIT_ACTION_ID_RESUME), k_resume_all,
                          sizeof(k_resume_all)));
  return sys_actions_set(SYS_ACTIONS_ID(SYS_ACTIONS_CLASS_USER, CONFIG_RUNIT_ACTION_ID_SUSPEND), k_suspend_all,
                         sizeof(k_suspend_all));
}
