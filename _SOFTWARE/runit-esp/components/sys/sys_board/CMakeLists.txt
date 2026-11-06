# The board's profile: the stored record of its devices and settings, replayed at boot, and the JSON the app reads back
# (SYS_BOARD.MD).
idf_component_register(
    SRCS "sys_board.c" "router/sys_board_router.c"
    INCLUDE_DIRS "." "router"
    REQUIRES errors cmd router sys_actions sys_nvs
    PRIV_REQUIRES utils
)
