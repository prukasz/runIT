idf_component_register(
    SRCS "sys_actions.c" "sys_actions_router.c"
    INCLUDE_DIRS "."
    REQUIRES errors utils cmd router
    PRIV_REQUIRES log nvs_flash esp_rom
)
