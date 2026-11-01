# The application: boot sequence, safe state, error policy responses, board stream routing (RUNIT.MD).
idf_component_register(
    SRCS "runit.c" "runit_board_cfg.c" "runit_error_policy.c" "runit_onboard_actions.c"
    INCLUDE_DIRS "."
    REQUIRES errors
    PRIV_REQUIRES log utils cmd router sys_i2c sys_uart sys_ble sys_data_connector sys_data_provider sys_session sys_watch sys_remote sys_device sys_actions sys_project sys_system sys_event sys_io sys_log sys_power sys_power_manager devices vm_core vm_blocks
)
