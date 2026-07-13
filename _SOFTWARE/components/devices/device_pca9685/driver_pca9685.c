#include "driver_pca9685.h"
#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char* TAG = __FILE_NAME__;

#undef OWNER
#define OWNER OWNER_DRIVER_PCA9685

#define REG_MODE1 0x00
#define REG_MODE2 0x01
#define REG_LED_START 0x06
#define REG_PRE_SCALE 0xFE

#define MODE1_SLEEP_BIT (1 << 4)
#define MODE1_AI (1 << 5)
#define LED_FULL_ON_OFF (1 << 4)

#define PCA9685_INTERNAL_FREQ 25000000UL
#define WAKEUP_DELAY_US 500
#define MIN_PRESCALER 0x03

#define REG_LED_N(x) (REG_LED_START + (x) * 4)

#define RETURN_ON_ERROR(x)                   \
  do {                                       \
    esp_err_t __err_rc = (x);                \
    if (__err_rc != ESP_OK) return __err_rc; \
  } while (0)

#define CHECK_DRV_HANDLE(VAL)               \
  do {                                      \
    if (!(VAL)) return ESP_ERR_INVALID_ARG; \
  } while (0)

static inline esp_err_t _update_reg(pca9685_handle_t handle, uint8_t reg, uint8_t mask, uint8_t val) {
  uint8_t r = 0;
  RETURN_ON_ERROR(sys_i2c_master_transmit_receive(handle, &reg, 1, &r, 1));
  r = (r & ~mask) | val;
  return sys_i2c_master_transmit(handle, (uint8_t[]){reg, r}, 2);
}

pca9685_handle_t pca9685_new(uint8_t i2c_address, bool i2c_bus_num) {
  pca9685_handle_t handle = calloc(1, sizeof(_pca9685_data_t));
  if (!handle) {
    ESP_LOGE(TAG, "Failed to allocate memory for PCA9685 handle");
    return NULL;
  }

  handle->header.i2c_device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  handle->header.i2c_device_config.device_address = i2c_address;
  handle->header.i2c_device_config.scl_speed_hz = PCA9685_I2C_DEFAULT_FREQUENCY;
  handle->header.bus_num = i2c_bus_num;

  return handle;
}

esp_err_t pca9685_start(pca9685_handle_t handle) {
  CHECK_DRV_HANDLE(handle);

  status_rep_t init_status = sys_i2c_add_driver(&handle->header);
  if (STA_IS_ERR(init_status)) {
    ESP_LOGE(TAG, "I2C Manager rejected PCA9685 on bus %d", handle->header.bus_num);
    return ESP_FAIL;
  }

  RETURN_ON_ERROR(pca9685_sleep(handle, false));
  RETURN_ON_ERROR(pca9685_enable_auto_increment(handle));

  ESP_LOGI(TAG, "PCA9685 started successfully on bus %d at 0x%02X", handle->header.bus_num, handle->header.i2c_device_config.device_address);
  return ESP_OK;
}

esp_err_t pca9685_set_pwm_value(pca9685_handle_t handle, uint8_t channel, uint16_t value) {
  CHECK_DRV_HANDLE(handle && channel < PCA9685_CHANNEL_ALL && value <= PCA9685_MAX_PWM_VALUE);

  handle->channel_pwm_value[channel] = value;

  bool full_on = (value >= PCA9685_MAX_PWM_VALUE);
  bool full_off = (value == 0);
  uint16_t raw = full_on ? 4095 : value;

  uint8_t buf[5];
  buf[0] = REG_LED_N(channel);
  buf[1] = 0;
  buf[2] = full_on ? LED_FULL_ON_OFF : 0;
  buf[3] = raw & 0xFF;
  buf[4] = full_off ? LED_FULL_ON_OFF : (raw >> 8);

  return sys_i2c_master_transmit(handle, buf, sizeof(buf));
}

esp_err_t pca9685_set_pwm_frequency(pca9685_handle_t handle, uint16_t freq) {
  CHECK_DRV_HANDLE(handle && freq != 0);

  uint8_t prescaler = (uint8_t)(round((float)PCA9685_INTERNAL_FREQ / (PCA9685_MAX_PWM_VALUE * freq))) - 1;
  if (prescaler < MIN_PRESCALER) {
    prescaler = MIN_PRESCALER;
  }

  handle->freq = freq;
  handle->prescale = prescaler;

  // To set prescaler, chip must be in sleep mode
  RETURN_ON_ERROR(pca9685_sleep(handle, true));
  RETURN_ON_ERROR(sys_i2c_master_transmit(handle, (uint8_t[]){REG_PRE_SCALE, prescaler}, 2));
  RETURN_ON_ERROR(pca9685_sleep(handle, false));

  return ESP_OK;
}

esp_err_t pca9685_sleep(pca9685_handle_t handle, bool sleep) {
  CHECK_DRV_HANDLE(handle);
  uint8_t val = sleep ? MODE1_SLEEP_BIT : 0;
  RETURN_ON_ERROR(_update_reg(handle, REG_MODE1, (uint8_t)MODE1_SLEEP_BIT, val));
  if (!sleep) {
    esp_rom_delay_us(WAKEUP_DELAY_US);
  }
  return ESP_OK;
}

esp_err_t pca9685_enable_auto_increment(pca9685_handle_t handle) {
  CHECK_DRV_HANDLE(handle);
  return _update_reg(handle, REG_MODE1, MODE1_AI, MODE1_AI);
}