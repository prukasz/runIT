# The handler, the op tables and the tree of module routers (router.h). WHOLE_ARCHIVE: the error tags are registered by a
# constructor in router.c, and a handler raises ERR_CMD_BAD_LEN / BAD_ARG without calling cmd_reply: the linker would
# drop the registration.
idf_component_register(
    SRCS "router.c"
    INCLUDE_DIRS "." "errors"
    REQUIRES errors utils
    WHOLE_ARCHIVE
)
