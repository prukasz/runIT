# The display contract (sys_display_contract.h) and its C API. ".." for "sys_display/..." includes.
# WHOLE_ARCHIVE: registers the errors of MODULE_ID_DISPLAY even when nothing calls a C API function directly.
idf_component_register(
    SRCS "sys_display.c"
    INCLUDE_DIRS "." "errors" ".."
    REQUIRES errors cmd sys_device sys_io
    WHOLE_ARCHIVE
)
