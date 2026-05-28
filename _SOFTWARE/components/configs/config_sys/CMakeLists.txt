idf_component_register(
    SRCS "config_sys.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES manager_io runit_kernel packet_interface
    REQUIRES status 
)