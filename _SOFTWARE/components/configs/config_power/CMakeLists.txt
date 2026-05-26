idf_component_register(
    SRCS "config_power.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES manager_io manager_power runit_kernel
    REQUIRES status 
)