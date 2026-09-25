idf_component_register(SRCS "runit.c" "runit_board_cfg.c" "runit_decoders.c" "runit_error_policy.c"
                    INCLUDE_DIRS "." "include"
                    REQUIRES freertos log devices sys_i2c sys_device sys_io sys_errors sys_power ble sys_buffers sys_interface codecs sys_actions VM sys_data_connector utils sys_settings sys_event sys_uart)
