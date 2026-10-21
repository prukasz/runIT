idf_component_register(
    SRCS "sys_project.c" "router/sys_project_router.c"
    INCLUDE_DIRS "." "errors" "router"
    REQUIRES errors utils cmd router sys_actions
    PRIV_REQUIRES sys_device log nvs_flash esp_rom
)
