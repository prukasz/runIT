# The events as a stream of the data matrix, both ways (SYS_EVENT_STREAM.MD). Apart from sys_event: sys_session needs
# sys_event's events and the matrix needs sys_session, so sys_event can't know the matrix.
idf_component_register(
    SRCS "sys_event_stream.c"
    INCLUDE_DIRS "."
    REQUIRES errors data_matrix
    PRIV_REQUIRES sys_event
)
