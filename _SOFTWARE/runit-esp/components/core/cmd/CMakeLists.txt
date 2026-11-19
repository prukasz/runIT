# Shared command contract (cmd.h, cmd_table.h) and ERR_CMD_* error tags.
# WHOLE_ARCHIVE: a handler raises ERR_CMD_* without calling any function in cmd.c, so the linker must not drop the
# error tag registration constructor in cmd.c.
idf_component_register(
    SRCS "cmd.c"
    INCLUDE_DIRS "." "errors"
    REQUIRES errors
    WHOLE_ARCHIVE
)
