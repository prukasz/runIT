# WHOLE_ARCHIVE: the error tags are registered by a constructor in sys_display.c;
# a driver may raise ERR_DISPLAY_* without calling a wrapper, and the linker would drop it.
idf_component_register(
    SRCS "sys_display.c"
    INCLUDE_DIRS "." "errors"
    REQUIRES errors cmd sys_device sys_event sys_io utils
    WHOLE_ARCHIVE
)
