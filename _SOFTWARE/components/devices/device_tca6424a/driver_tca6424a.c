#include "driver_tca6424a.h"
#include <esp_log.h>

static const char* TAG = __FILE_NAME__;

#undef OWNER
#define OWNER OWNER_DRIVER_TCA6424A

#define _PORT0_MASK 0x000000FF
#define _PORT1_MASK 0x0000FF00
#define _PORT2_MASK 0x00FF0000

#define TCA6424A_REG_INPUT_PORT0 0x00
#define TCA6424A_REG_OUTPUT_PORT0 0x04
#define TCA6424A_REG_POLARITY_PORT0 0x08
#define TCA6424A_REG_CONFIG_PORT0 0x0C
#define TCA6424A_AUTO_INCREMENT 0x80

#define RETURN_ON_ERROR(x)                   \
  do {                                       \
    esp_err_t __err_rc = (x);                \
    if (__err_rc != ESP_OK) return __err_rc; \
  } while (0)

#undef CHECK_HANDLE_R
#define CHECK_DRV_HANDLE(VAL)                 \
  do {                                      \
    if (!(VAL)) return ESP_ERR_INVALID_ARG; \
  } while (0)

#define TCA_TRANSMIT(handle, buf, size) ((handle)->header.transmit ? (handle)->header.transmit((handle), (buf), (size)) : ESP_ERR_INVALID_STATE)

#define TCA_TRANSMIT_RECEIVE(handle, tx_buf, tx_size, rx_buf, rx_size) ((handle)->header.transmit_receive ? (handle)->header.transmit_receive((handle), (tx_buf), (tx_size), (rx_buf), (rx_size)) : ESP_ERR_INVALID_STATE)

static esp_err_t _tca_update_ports(tca6424a_handle_t handle) {
  CHECK_DRV_HANDLE(handle);
  uint8_t buf[4] = {TCA6424A_REG_OUTPUT_PORT0 | TCA6424A_AUTO_INCREMENT, handle->output[0], handle->output[1], handle->output[2]};
  return TCA_TRANSMIT(handle, buf, 4);
}

static esp_err_t _tca_update_inputs(tca6424a_handle_t handle) {
  CHECK_DRV_HANDLE(handle);
  uint8_t reg = TCA6424A_REG_INPUT_PORT0 | TCA6424A_AUTO_INCREMENT;
  return TCA_TRANSMIT_RECEIVE(handle, &reg, 1, handle->last_read_input, 3);
}

static esp_err_t _tca_update_config(tca6424a_handle_t handle) {
  CHECK_DRV_HANDLE(handle);
  uint8_t buf[4] = {TCA6424A_REG_CONFIG_PORT0 | TCA6424A_AUTO_INCREMENT, handle->config[0], handle->config[1], handle->config[2]};
  return TCA_TRANSMIT(handle, buf, 4);
}

static esp_err_t _tca_update_polarity(tca6424a_handle_t handle) {
  CHECK_DRV_HANDLE(handle);
  uint8_t buf[4] = {TCA6424A_REG_POLARITY_PORT0 | TCA6424A_AUTO_INCREMENT, handle->polarity_cfg[0], handle->polarity_cfg[1], handle->polarity_cfg[2]};
  return TCA_TRANSMIT(handle, buf, 4);
}

/*******************************************************************************
 * API FUNCTIONS
 ******************************************************************************/

esp_err_t tca_set_pins(tca6424a_handle_t handle, uint32_t pins_mask, uint32_t pins_state) {
  uint8_t p0_mask = (pins_mask & _PORT0_MASK);
  uint8_t p0_state = (pins_state & _PORT0_MASK);
  handle->output[0] = (handle->output[0] & ~p0_mask) | (p0_state & p0_mask);

  uint8_t p1_mask = (pins_mask & _PORT1_MASK) >> 8;
  uint8_t p1_state = (pins_state & _PORT1_MASK) >> 8;
  handle->output[1] = (handle->output[1] & ~p1_mask) | (p1_state & p1_mask);

  uint8_t p2_mask = (pins_mask & _PORT2_MASK) >> 16;
  uint8_t p2_state = (pins_state & _PORT2_MASK) >> 16;
  handle->output[2] = (handle->output[2] & ~p2_mask) | (p2_state & p2_mask);

  return _tca_update_ports(handle);
}

esp_err_t tca_get_pins(tca6424a_handle_t handle, uint32_t* out_level) {
  esp_err_t err = _tca_update_inputs(handle);
  if (err != ESP_OK) {
    return err;
  }
  *out_level = (handle->last_read_input[2] << 16) | (handle->last_read_input[1] << 8) | handle->last_read_input[0];
  return ESP_OK;
}

esp_err_t tca_preset_cfg(tca6424a_handle_t handle, uint32_t cfg_mask, uint32_t cfg_state) {
  uint8_t cfg0_mask = (cfg_mask & _PORT0_MASK);
  uint8_t cfg0_state = (cfg_state & _PORT0_MASK);
  handle->config[0] = (handle->config[0] & ~cfg0_mask) | (cfg0_state & cfg0_mask);

  uint8_t cfg1_mask = (cfg_mask & _PORT1_MASK) >> 8;
  uint8_t cfg1_state = (cfg_state & _PORT1_MASK) >> 8;
  handle->config[1] = (handle->config[1] & ~cfg1_mask) | (cfg1_state & cfg1_mask);

  uint8_t cfg2_mask = (cfg_mask & _PORT2_MASK) >> 16;
  uint8_t cfg2_state = (cfg_state & _PORT2_MASK) >> 16;
  handle->config[2] = (handle->config[2] & ~cfg2_mask) | (cfg2_state & cfg2_mask);

  return _tca_update_config(handle);
}

esp_err_t tca_set_polarity(tca6424a_handle_t handle, uint32_t polarity_mask, uint32_t polarity_state) {
  uint8_t pol0_mask = (polarity_mask & _PORT0_MASK);
  uint8_t pol0_state = (polarity_state & _PORT0_MASK);
  handle->polarity_cfg[0] = (handle->polarity_cfg[0] & ~pol0_mask) | (pol0_state & pol0_mask);

  uint8_t pol1_mask = (polarity_mask & _PORT1_MASK) >> 8;
  uint8_t pol1_state = (polarity_state & _PORT1_MASK) >> 8;
  handle->polarity_cfg[1] = (handle->polarity_cfg[1] & ~pol1_mask) | (pol1_state & pol1_mask);

  uint8_t pol2_mask = (polarity_mask & _PORT2_MASK) >> 16;
  uint8_t pol2_state = (polarity_state & _PORT2_MASK) >> 16;
  handle->polarity_cfg[2] = (handle->polarity_cfg[2] & ~pol2_mask) | (pol2_state & pol2_mask);

  return _tca_update_polarity(handle);
}

uint32_t tca_get_pin_output(tca6424a_handle_t handle) { return (handle->output[2] << 16) | (handle->output[1] << 8) | handle->output[0]; }

esp_err_t tca_restore_state(tca6424a_handle_t handle) {
  if (!handle) return ESP_ERR_INVALID_ARG;
  esp_err_t err;
  err = _tca_update_config(handle);
  if (err != ESP_OK) return err;
  err = _tca_update_ports(handle);
  if (err != ESP_OK) return err;
  return _tca_update_polarity(handle);
}



/*******************************************************************************
 * TWO-PHASE INITIALIZATION
 ******************************************************************************/

tca6424a_handle_t d_tca6424a_new(uint8_t i2c_address, bool i2c_bus_num) {
  tca6424a_handle_t handle = calloc(1, sizeof(tca_data_t));
  if (!handle) {
    ESP_LOGE(TAG, "Failed to allocate memory for TCA6424A handle");
    return NULL;
  }

  handle->header.i2c_device_config.device_address = i2c_address;
  handle->header.i2c_device_config.scl_speed_hz = 400000;
  handle->header.i2c_device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  handle->header.bus_num = i2c_bus_num;

  tca_restore_state(handle);

  return handle;
}

void d_tca6424a_delete(tca6424a_handle_t handle) {
  if (!handle) return;
  free(handle);
}
