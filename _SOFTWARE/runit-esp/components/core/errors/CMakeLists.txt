# The error chains (ERRORS.MD): the nodes and their pool, the registry of the modules' tags, the log, the error record
# and the reports. The node's layout and what only this component's files share are in internal/ (err_internal.h).
idf_component_register(
    SRCS "errors.c" "err_pool.c" "err_tags.c" "err_log.c" "err_record.c" "err_sink.c"
    INCLUDE_DIRS "." "errors"
    PRIV_INCLUDE_DIRS "internal"
    REQUIRES utils
    PRIV_REQUIRES log time
)
