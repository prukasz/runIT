# Module op tables and the tree of module routers (router.h). WHOLE_ARCHIVE: the error tags are registered by a
# constructor in router.c.
idf_component_register(
    SRCS "router.c"
    INCLUDE_DIRS "." "errors"
    REQUIRES cmd errors utils
    WHOLE_ARCHIVE
)
