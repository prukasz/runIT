import ctypes as ct
from enum import IntEnum


# ============================================================================
# INTERFACE_COMMANDS CONFIGURATION STRUCTURES
# ============================================================================

class packet_header_t(IntEnum):
    PACKET_H_CFG_PWR = 0x01
    PACKET_H_CFG_IO = 0x02
    PACKET_H_CFG_SYS = 0x03
    PACKET_H_CFG_TESTS = 0x04

# ============================================================================
# CONFIG_SYS CONFIGURATION STRUCTURES
# ============================================================================

class cfg_sys_packet_type_e(IntEnum):
    CFG_SYS_TYPE_LOG_CONFIG = 0
    CFG_SYS_TYPE_DEVICE_DEFAULT = 1
    CFG_SYS_TYPE_VM_RUN = 2
    CFG_SYS_TYPE_VM_STOP = 3
    CFG_SYS_TYPE_VM_EMERGENCY = 4

class cfg_log_level_e(IntEnum):
    LOG_NONE = 0
    LOG_ERROR = 1
    LOG_WARN = 2
    LOG_INFO = 3
    LOG_DEBUG = 4
    LOG_VERBOSE = 5

class cfg_log_t(ct.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("enable_stream", ct.c_bool),
        ("mirror_on_serial", ct.c_bool),
        ("esp_log_level", ct.c_uint8),
    ]
    _hints_ = {
        "esp_log_level": cfg_log_level_e,
    }
    _packet_header_ = cfg_sys_packet_type_e.CFG_SYS_TYPE_LOG_CONFIG

# ============================================================================
# CONFIG_IO CONFIGURATION STRUCTURES
# ============================================================================

class cfg_io_packet_type_e(IntEnum):
    CFG_IO_TYPE_GPIO_MODE = 0
    CFG_IO_TYPE_GPIO_ADC_ALERT = 1
    CFG_IO_TYPE_GPIO_INTERRUPT = 2
    CFG_IO_TYPE_GPIO_PWM_FREQ = 3
    CFG_IO_TYPE_GPIO_RESET = 4

class cfg_io_adc_window_mode_e(IntEnum):
    OUTSIDE_WINDOW = 0
    INSIDE_WINDOW = 1

class cfg_io_gpio_mode_e(IntEnum):
    INPUT = 0
    OUTPUT_PUSH_PULL = 1
    OUTPUT_OPEN_DRAIN = 2
    INPUT_PULLUP = 3
    INPUT_PULLDOWN = 4
    PWM = 5
    ADC = 6

class cfg_gpio_intr_mode_e(IntEnum):
    RISING_EDGE = 0
    FALLING_EDGE = 1
    BOTH_EDGES = 2
    LEVEL_HIGH = 3
    LEVEL_LOW = 4

class cfg_io_gpio_mode_t(ct.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("pin_id", ct.c_uint32),
        ("mode", ct.c_uint32),
    ]
    _hints_ = {
        "mode": cfg_io_gpio_mode_e,
    }
    _packet_header_ = cfg_io_packet_type_e.CFG_IO_TYPE_GPIO_MODE

class cfg_gpio_intr_mode_t(ct.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("pin_id", ct.c_uint32),
        ("cfg_gpio_intr_mode", ct.c_uint32),
    ]
    _hints_ = {
        "cfg_gpio_intr_mode": cfg_gpio_intr_mode_e,
    }
    _packet_header_ = cfg_io_packet_type_e.CFG_IO_TYPE_GPIO_INTERRUPT

class cfg_io_gpio_reset_t(ct.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("pin_id", ct.c_uint32),
    ]
    _packet_header_ = cfg_io_packet_type_e.CFG_IO_TYPE_GPIO_RESET

class cfg_io_gpio_adc_alert_t(ct.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("pin_id", ct.c_uint32),
        ("adc_threshold_up_mv", ct.c_uint32),
        ("adc_threshold_down_mv", ct.c_uint32),
        ("adc_threshold_hysteresis_mv", ct.c_uint32),
        ("adc_event_counter_threshold", ct.c_uint32),
        ("adc_window_mode", ct.c_uint32),
    ]
    _hints_ = {
        "adc_window_mode": cfg_io_adc_window_mode_e,
    }
    _packet_header_ = cfg_io_packet_type_e.CFG_IO_TYPE_GPIO_ADC_ALERT

class cfg_io_gpio_pwm_freq_t(ct.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("pin_id", ct.c_uint64),
        ("freq_hz", ct.c_uint32),
    ]
    _packet_header_ = cfg_io_packet_type_e.CFG_IO_TYPE_GPIO_PWM_FREQ

# ============================================================================
# CONFIG_POWER CONFIGURATION STRUCTURES
# ============================================================================

class cfg_pwr_packet_type_e(IntEnum):
    CFG_PWR_TYPE_REG_EN = 1
    CFG_PWR_TYPE_REG_SETTINGS = 2
    CFG_PWR_TYPE_REG_LIMITS = 3
    CFG_PWR_TYPE_REG_BEHAVIOR = 4
    CFG_PWR_TYPE_SUPPLY = 11
    CFG_PWR_TYPE_CURRENT_BEHAVIOR = 13
    CFG_PWR_TYPE_TEST_SET_PD = 14
    CFG_PWR_TYPE_TEST_GET_PD_VOLTAGE = 15
    CFG_PWR_TYPE_TEST_GET_PD_CURRENT = 16

class cfg_pwr_error_behavior_e(IntEnum):
    AUTOMATIC = 0
    IGNORE = 1
    VM_CALLBACKS_ONLY = 2
    STOP = 3
    EMERGENCY = 4
    DISABLE_DEVICE = 5

class cfg_pwr_reg_en_t(ct.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("en_reg_0", ct.c_bool),
        ("en_reg_1", ct.c_bool),
    ]
    _packet_header_ = cfg_pwr_packet_type_e.CFG_PWR_TYPE_REG_EN

class cfg_pwr_reg_settings_t(ct.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("voltage_reg_0_mV", ct.c_uint32),
        ("voltage_reg_1_mV", ct.c_uint32),
        ("current_limit_reg_0_mA", ct.c_uint32),
        ("current_limit_reg_1_mA", ct.c_uint32),
    ]
    _packet_header_ = cfg_pwr_packet_type_e.CFG_PWR_TYPE_REG_SETTINGS

class cfg_pwr_reg_limits_t(ct.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("power_warning_reg_0_mW", ct.c_uint32),
        ("power_critical_reg_0_mW", ct.c_uint32),
        ("power_warning_reg_1_mW", ct.c_uint32),
        ("power_critical_reg_1_mW", ct.c_uint32),
    ]
    _packet_header_ = cfg_pwr_packet_type_e.CFG_PWR_TYPE_REG_LIMITS

class cfg_pwr_reg_behavior_t(ct.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("behavior_reg0_ovp", ct.c_uint8),
        ("behavior_reg0_ocp", ct.c_uint8),
        ("behavior_reg0_scp", ct.c_uint8),
        ("behavior_reg1_ovp", ct.c_uint8),
        ("behavior_reg1_ocp", ct.c_uint8),
        ("behavior_reg1_scp", ct.c_uint8),
    ]
    _hints_ = {
        "behavior_reg0_ovp": cfg_pwr_error_behavior_e,
        "behavior_reg0_ocp": cfg_pwr_error_behavior_e,
        "behavior_reg0_scp": cfg_pwr_error_behavior_e,
        "behavior_reg1_ovp": cfg_pwr_error_behavior_e,
        "behavior_reg1_ocp": cfg_pwr_error_behavior_e,
        "behavior_reg1_scp": cfg_pwr_error_behavior_e,
    }
    _packet_header_ = cfg_pwr_packet_type_e.CFG_PWR_TYPE_REG_BEHAVIOR

class cfg_pwr_supply_t(ct.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("provided_input_voltage_mv", ct.c_uint32),
        ("provided_input_current_ma", ct.c_uint32),
        ("input_voltage_warning_mV", ct.c_uint32),
        ("input_voltage_critical_mV", ct.c_uint32),
        ("input_current_warning_mA", ct.c_int32),
        ("input_current_critical_mA", ct.c_int32),
        ("input_voltage_to_negotiate_mv", ct.c_uint32),
        ("input_current_to_negotiate_ma", ct.c_uint32),
    ]
    _packet_header_ = cfg_pwr_packet_type_e.CFG_PWR_TYPE_SUPPLY

class cfg_pwr_current_behavior_t(ct.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("behavior_current_REG0_WARN", ct.c_uint8),
        ("behavior_current_REG0_CRIT", ct.c_uint8),
        ("behavior_current_REG1_WARN", ct.c_uint8),
        ("behavior_current_REG1_CRIT", ct.c_uint8),
        ("behavior_current_SYS_PWR_WARN", ct.c_uint8),
        ("behavior_current_SYS_PWR_CRIT", ct.c_uint8),
    ]
    _hints_ = {
        "behavior_current_REG0_WARN": cfg_pwr_error_behavior_e,
        "behavior_current_REG0_CRIT": cfg_pwr_error_behavior_e,
        "behavior_current_REG1_WARN": cfg_pwr_error_behavior_e,
        "behavior_current_REG1_CRIT": cfg_pwr_error_behavior_e,
        "behavior_current_SYS_PWR_WARN": cfg_pwr_error_behavior_e,
        "behavior_current_SYS_PWR_CRIT": cfg_pwr_error_behavior_e,
    }
    _packet_header_ = cfg_pwr_packet_type_e.CFG_PWR_TYPE_CURRENT_BEHAVIOR

class cfg_pwr_test_set_pd_t(ct.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("pd_voltage_mv", ct.c_uint32),
        ("pd_current_ma", ct.c_uint32),
    ]
    _packet_header_ = cfg_pwr_packet_type_e.CFG_PWR_TYPE_TEST_SET_PD

# ============================================================================
# CONFIG_TESTS CONFIGURATION STRUCTURES
# ============================================================================

class cfg_test_packet_type_e(IntEnum):
    CFG_TEST_TYPE_GPIO_SET_LEVEL = 1
    CFG_TEST_TYPE_GPIO_GET_LEVEL = 2
    CFG_TEST_TYPE_GPIO_TOGGLE = 3
    CFG_TEST_TYPE_ADC_READ_MV = 4
    CFG_TEST_TYPE_GPIO_PWM_DUTY = 5
    CFG_TEST_TYPE_RESET_ALL = 6
    CFG_TEST_TYPE_GET_REG_VOLTAGE = 7
    CFG_TEST_TYPE_GET_REG_CURRENT = 8
    CFG_TEST_TYPE_GET_SYS_VOLTAGE = 9
    CFG_TEST_TYPE_GET_SYS_CURRENT = 10

class cfg_test_gpio_set_level_t(ct.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("pin_id", ct.c_uint32),
        ("level", ct.c_bool),
    ]
    _packet_header_ = cfg_test_packet_type_e.CFG_TEST_TYPE_GPIO_SET_LEVEL

class cfg_test_gpio_get_level_t(ct.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("pin_id", ct.c_uint32),
    ]
    _packet_header_ = cfg_test_packet_type_e.CFG_TEST_TYPE_GPIO_GET_LEVEL

class cfg_test_gpio_toggle_t(ct.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("pin_id", ct.c_uint32),
    ]
    _packet_header_ = cfg_test_packet_type_e.CFG_TEST_TYPE_GPIO_TOGGLE

class cfg_test_adc_read_mv_t(ct.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("pin_id", ct.c_uint32),
    ]
    _packet_header_ = cfg_test_packet_type_e.CFG_TEST_TYPE_ADC_READ_MV

class cfg_test_gpio_pwm_duty_t(ct.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("pin_id", ct.c_uint64),
        ("duty_cycle", ct.c_uint32),
    ]
    _packet_header_ = cfg_test_packet_type_e.CFG_TEST_TYPE_GPIO_PWM_DUTY

class cfg_test_get_reg_voltage_t(ct.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("regulator_num", ct.c_uint8),
    ]
    _packet_header_ = cfg_test_packet_type_e.CFG_TEST_TYPE_GET_REG_VOLTAGE

class cfg_test_get_reg_current_t(ct.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("regulator_num", ct.c_uint8),
    ]
    _packet_header_ = cfg_test_packet_type_e.CFG_TEST_TYPE_GET_REG_CURRENT
