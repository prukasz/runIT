# The application: boot sequence, safe state, error policy responses, board stream routing (RUNIT.MD).
idf_component_register(
    SRCS "runit.c" "runit_board_cfg.c" "runit_error_policy.c"
    INCLUDE_DIRS "."
    REQUIRES errors
    PRIV_REQUIRES log sys_nvs sys_board utils cmd router sys_i2c sys_uart sys_ble sys_stream sys_data_provider sys_session sys_watch sys_remote sys_device sys_actions sys_project sys_ota sys_system sys_event sys_io sys_log sys_power sys_power_manager devices vm_core vm_blocks
)
