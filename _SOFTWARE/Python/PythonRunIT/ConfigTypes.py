import ctypes as ct
from enum import IntEnum


# ============================================================================
# INTERFACE_COMMANDS CONFIGURATION STRUCTURES
# ============================================================================

class packet_header_t(IntEnum):
    PACKET_H_CFG_PWR = 0x01
    PACKET_H_CFG_IO = 0x02
    PACKET_H_CFG_SYS = 0x03

# ============================================================================
# CONFIG_POWER CONFIGURATION STRUCTURES
# ============================================================================

class cfg_pwr_packet_type_e(IntEnum):
    CFG_PWR_TYPE_REG_EN = 1
    CFG_PWR_TYPE_REG_SETTINGS = 2
    CFG_PWR_TYPE_REG_LIMITS = 3
    CFG_PWR_TYPE_REG_BEHAVIOR = 4
    CFG_PWR_TYPE_SUPPLY = 11
    CFG_PWR_TYPE_SUPPLY_LIMITS = 12
    CFG_PWR_TYPE_SUPPLY_BEHAVIOR = 13

class cfg_pwr_reg_en_t(ct.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("regulator_num", ct.c_uint8),
        ("enable", ct.c_uint8),
    ]

class cfg_pwr_reg_settings_t(ct.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("regulator_num", ct.c_uint8),
        ("voltage_mv", ct.c_uint32),
        ("current_limit_ma", ct.c_uint32),
    ]

class cfg_pwr_reg_limits_t(ct.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("power_warning_reg_0_mW", ct.c_uint32),
        ("power_critical_reg_0_mW", ct.c_uint32),
        ("power_warning_reg_1_mW", ct.c_uint32),
        ("power_critical_reg_1_mW", ct.c_uint32),
    ]

class cfg_pwr_reg_behavior_t(ct.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("reg_number", ct.c_uint32, 1),
        ("over_budget_warning", ct.c_uint32, 1),
        ("over_budget_critical", ct.c_uint32, 1),
        ("off_on_short_circuit", ct.c_uint32, 1),
        ("off_on_over_voltage", ct.c_uint32, 1),
        ("off_on_over_current", ct.c_uint32, 1),
        ("_reserved", ct.c_uint32, 26),
    ]

class cfg_pwr_supply_t(ct.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("provided_input_voltage_mv", ct.c_uint32),
        ("provided_input_current_ma", ct.c_uint32),
        ("input_voltage_warning_mV", ct.c_uint32),
        ("input_current_warning_ma", ct.c_uint32),
        ("input_voltage_to_negotiate_mv", ct.c_uint32),
        ("input_current_to_negotiate_ma", ct.c_uint32),
    ]

class cfg_pwr_supply_limits_t(ct.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("power_warning_total_mW", ct.c_uint32),
        ("power_critical_total_mW", ct.c_uint32),
    ]

class cfg_pwr_supply_behavior_t(ct.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("over_budget_warning", ct.c_uint32, 1),
        ("over_budget_critical", ct.c_uint32, 1),
        ("reserved", ct.c_uint32, 30),
    ]

# ============================================================================
# CONFIG_IO CONFIGURATION STRUCTURES
# ============================================================================

class cfg_io_packet_type_e(IntEnum):
    CFG_IO_TYPE_GPIO_MODE = 0
    CFG_IO_TYPE_GPIO_ADC_ALERT = 1
    CFG_IO_TYPE_GPIO_PWM_FREQ = 2

class cfg_io_gpio_mode_t(ct.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("pin_id", ct.c_uint64),
        ("mode", ct.c_uint32),
    ]

class cfg_io_gpio_adc_alert_t(ct.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("pin_id", ct.c_uint32),
        ("adc_threshold_down_mv", ct.c_uint32),
        ("adc_threshold_hysteresis_mv", ct.c_uint32),
        ("adc_event_counter_threshold", ct.c_uint32),
        ("adc_window_mode", ct.c_uint32),
    ]

class cfg_io_gpio_pwm_freq_t(ct.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("pin_id", ct.c_uint64),
        ("freq_hz", ct.c_uint32),
    ]

# ============================================================================
# CONFIG_SYS CONFIGURATION STRUCTURES
# ============================================================================

class cfg_log_level_e(IntEnum):
    CFG_ESP_LOG_NONE = 0
    CFG_ESP_LOG_ERROR = 1
    CFG_ESP_LOG_WARN = 2
    CFG_ESP_LOG_INFO = 3
    CFG_ESP_LOG_DEBUG = 4
    CFG_ESP_LOG_VERBOSE = 5
    CFG_SP_LOG_MAX = 6

class cfg_sys_ctrl_mode_e(IntEnum):
    CFG_SYS_SYS_CTRL_AUTOMATIC = 0x00
    CFG_SYS_SYS_CTRL_SKIP_EVENT = 0x01
    CFG_SYS_SYS_CTRL_VM_CALLBACKS_ONLY = 0x02
    CFG_SYS_SYS_CTRL_STOP = 0x03
    CFG_SYS_CTRL_ENTER_EMERGENCY = 0x04
    CFG_SYS_CTRL_DISABLE_DEVICE = 0x05

class cfg_system_ctrl_t(ct.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("crt_reg0_ovp", ct.c_uint8),
        ("crt_reg0_ocp", ct.c_uint8),
        ("crt_reg0_scp", ct.c_uint8),
        ("crt_reg1_ovp", ct.c_uint8),
        ("crt_reg1_ocp", ct.c_uint8),
        ("crt_reg1_scp", ct.c_uint8),
        ("crt_current_REG0_WARN", ct.c_uint8),
        ("crt_current_REG0_CRIT", ct.c_uint8),
        ("crt_current_REG1_WARN", ct.c_uint8),
        ("crt_current_REG1_CRIT", ct.c_uint8),
        ("crt_current_SYS_PWR_WARN", ct.c_uint8),
        ("crt_current_SYS_PWR_CRIT", ct.c_uint8),
    ]
