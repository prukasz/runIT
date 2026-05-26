idf_component_register(
    SRCS "config_io.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES manager_io runit_kernel packet_interface
    REQUIRES status 
)