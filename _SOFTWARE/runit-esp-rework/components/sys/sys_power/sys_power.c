#include <string.h>

#include "sys_power_monitor.h"
#include "sys_power_supply.h"
#include "sys_power_usb_pd.h"
#include "sys_power_vreg.h"

ERR_MODULE_DEFINE(ERR_MOD_SYS_POWER, "sys_power", ERR_SYS_POWER_MAP)

MODULE_ID_DEFINE(SYS_POWER, 0x26)
SYS_DEVICE_CONTRACT_DEFINE(PMON, 0x04)
SYS_DEVICE_CONTRACT_DEFINE(VREG, 0x05)
SYS_DEVICE_CONTRACT_DEFINE(PD, 0x07)
SYS_DEVICE_CONTRACT_DEFINE(SUPPLY, 0x08)

err_h sys_power_usb_pd_list_options(uint8_t device, sys_power_usb_pd_option_t* out, uint8_t max_options,
                                    uint8_t* out_count) {
  ERR_CHECK(out != NULL && out_count != NULL && max_options <= SYS_POWER_USB_PD_OPTIONS_MAX, ERR_DEV_BAD_ARG);
  sys_power_usb_pd_option_t all[SYS_POWER_USB_PD_OPTIONS_MAX];
  cmd_out_t answer = {.data = (uint8_t*)all, .cap = sizeof(all), .len = 0};
  ERR_TRY(sys_device_call(device, CMD_DIR_READ, PD_OP_OPTIONS, (cmd_in_t){NULL, 0}, &answer));
  ERR_CHECK(answer.len % sizeof(all[0]) == 0, ERR_CMD_BAD_LEN, .got = (uint16_t)answer.len,
            .need = (uint16_t)sizeof(all[0]));
  uint8_t count = (uint8_t)(answer.len / sizeof(all[0]));
  count = count > max_options ? max_options : count;
  memcpy(out, all, count * sizeof(all[0]));
  *out_count = count;
  return NULL;
}
