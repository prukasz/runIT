# The events as a port of the data matrix, both ways (SYS_EVENT_STREAM.MD). Apart from sys_event, which knows no port.
# WHOLE_ARCHIVE: registers its port and listener in a constructor, with no symbol called from outside.
idf_component_register(
    SRCS "sys_event_stream.c"
    INCLUDE_DIRS "."
    REQUIRES errors data_matrix
    PRIV_REQUIRES cmd sys_event
    WHOLE_ARCHIVE
)
