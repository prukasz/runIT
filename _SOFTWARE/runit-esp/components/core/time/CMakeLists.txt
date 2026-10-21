# The header is board_time.h, not time.h: this folder is on every dependent's include path and would shadow libc's <time.h>.
idf_component_register(
    SRCS "board_time.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_timer
)
