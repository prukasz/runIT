# Events (sys_event.h): the layer below the device registry, so it requires nothing of sys_device. The header is
# board_time.h of core/time (the event's stamp); the queue and the task are the static objects of utils.
idf_component_register(
    SRCS "sys_event.c"
    INCLUDE_DIRS "." "errors"
    REQUIRES errors
    PRIV_REQUIRES utils time freertos
)
