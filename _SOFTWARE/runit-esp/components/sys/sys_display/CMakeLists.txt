# The display contract (sys_display_contract.h) and its C API. ".." for "sys_display/..." includes.
idf_component_register(
    SRCS "sys_display.c"
    INCLUDE_DIRS "." "errors" ".."
    REQUIRES errors cmd sys_device sys_io
)
