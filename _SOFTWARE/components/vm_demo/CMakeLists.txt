idf_component_register(
    SRCS "vm_demo.c"
    INCLUDE_DIRS "include"
    REQUIRES manager_io status utils esc_servo
    PRIV_REQUIRES runit_kernel
)
