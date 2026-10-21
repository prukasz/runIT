# WHOLE_ARCHIVE: the error tags are registered by a constructor in sys_power.c;
# a device may raise ERR_POWER_* without calling a wrapper, and the linker would drop it.
idf_component_register(
    SRCS "sys_power.c"
    INCLUDE_DIRS "." "contracts"
    REQUIRES errors cmd sys_device sys_event
    WHOLE_ARCHIVE
)
