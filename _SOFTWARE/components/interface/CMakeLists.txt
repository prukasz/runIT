idf_component_register(
    SRCS "interface_dispatcher.c" "configs/config_io.c" "configs/config_power.c" "configs/config_sys.c"
    INCLUDE_DIRS "include" "configs/include"
    REQUIRES esp_ringbuf 
    PRIV_REQUIRES runit_kernel utils manager_io manager_power 
)
