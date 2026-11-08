#include "runit_board_cfg.h"

#include "runit.h"
#include "sdkconfig.h"
#include "sys_data_provider.h"
#include "sys_device.h"
#include "sys_device_types.h"
#include "sys_power_manager.h"
#include "sys_stream.h"
#include "sys_stream_router.h"
#include "utils.h"

/* ---- Streams ---- */

/* The board's own streams (SYS_STREAM.MD "Model"), all OUT: the errors and the events go first on every way out. */
static const sys_stream_cfg_t k_streams[] = {
    {.id = SYS_STREAM_LOGS, .dir = SYS_STREAM_OUT},
    {.id = SYS_STREAM_ERRORS, .dir = SYS_STREAM_OUT, .flags = SYS_STREAM_FLAG_URGENT},
    {.id = SYS_STREAM_TELEMETRY, .dir = SYS_STREAM_OUT},
    {.id = SYS_STREAM_EVENTS, .dir = SYS_STREAM_OUT, .flags = SYS_STREAM_FLAG_URGENT},
};

err_h runit_board_streams_init(void) {
  for (uint32_t i = 0; i < ARRAY_LEN(k_streams); i++) {
    ERR_TRY(sys_stream_create(&k_streams[i]));
  }
  sys_stream_mark_system();
  return NULL;
}

/* Routing: the log lines go as they are on the BLE LOGS characteristic. Every other stream goes to the clients that
 * subscribe to it (SUBSCRIBE, over their session). */
err_h runit_board_stream_bindings_init(void) {
  return sys_stream_bind(SYS_STREAM_LOGS, RUNIT_BOARD_PROVIDER_BLE_LOGS);
}

/* The ESP32-S3's own GPIO: the one device the board has before its profile (SYS_BOARD.MD). The profile's frames name its
 * pins (buses, modes, levels) and create everything else; it takes no configuration and exists once per build. */
err_h runit_board_gpio_init(void) {
  const uint16_t type = SYS_DEVICE_TYPE_ESP32S3_GPIO;
  const uint8_t args[SYS_DEVICE_CREATE_HDR] = {(uint8_t)type, (uint8_t)(type >> 8), SYS_DEVICE_CREATE_SYSTEM};
  return sys_device_write(RUNIT_BOARD_GPIO_DEVICE_ID, SYS_DEVICE_OP_CREATE, args, sizeof(args));
}

err_h runit_board_power_init(void) {
  return sys_power_manager_init(runit_power_safe_state);
}
