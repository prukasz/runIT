# The events as a port of the data matrix, both ways (SYS_EVENT_STREAM.MD). Apart from sys_event, which knows no port.
idf_component_register(
    SRCS "sys_event_stream.c"
    INCLUDE_DIRS "."
    REQUIRES errors data_matrix
    PRIV_REQUIRES cmd sys_event
)
