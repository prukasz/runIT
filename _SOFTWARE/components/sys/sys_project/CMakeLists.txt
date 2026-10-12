idf_component_register(
    SRCS "sys_project.c" "sys_project_router.c"
    INCLUDE_DIRS "."
    REQUIRES errors utils cmd router sys_actions
    PRIV_REQUIRES log nvs_flash esp_rom
)
