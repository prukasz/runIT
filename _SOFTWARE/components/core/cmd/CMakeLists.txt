# WHOLE_ARCHIVE: the error tags are registered by a constructor in cmd.c; a handler raises
# ERR_CMD_BAD_LEN / BAD_ARG without calling cmd_reply, and the linker would drop the registration.
idf_component_register(
    SRCS "cmd.c"
    INCLUDE_DIRS "."
    REQUIRES errors utils
    WHOLE_ARCHIVE
)
