idf_component_register(
    SRCS "interface_dispatcher.c" "configs/config_io.c" "configs/config_power.c" "configs/config_sys.c"
    INCLUDE_DIRS "include" "configs/include"
    REQUIRES esp_ringbuf runit_kernel
    PRIV_REQUIRES  utils manager_io manager_power 
)
