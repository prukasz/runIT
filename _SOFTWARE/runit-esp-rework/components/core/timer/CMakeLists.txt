# The board's timers (board_timer.h): one task runs every timer's callback. The header is board_timer.h, beside
# board_time.h (core/time).
idf_component_register(
    SRCS "board_timer.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES freertos utils
)
