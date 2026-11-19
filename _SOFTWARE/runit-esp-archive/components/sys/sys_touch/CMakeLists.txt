# WHOLE_ARCHIVE: the error tags are registered by a constructor in sys_touch.c;
# a driver may raise ERR_TOUCH_* without calling a wrapper, and the linker would drop it.
idf_component_register(
    SRCS "sys_touch.c"
    INCLUDE_DIRS "." "errors"
    REQUIRES errors cmd sys_device sys_io
    WHOLE_ARCHIVE
)
