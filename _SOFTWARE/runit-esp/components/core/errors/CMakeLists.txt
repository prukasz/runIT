idf_component_register(
    SRCS "err_wire.c" "errors.c" "err_pool.c" "err_tags.c" "err_log.c"
    INCLUDE_DIRS "."
    REQUIRES utils
    PRIV_REQUIRES log time
)
