idf_component_register(
    SRCS "sys_ota.c" "router/sys_ota_router.c"
    INCLUDE_DIRS "." "errors" "router"
    REQUIRES errors utils cmd router
    PRIV_REQUIRES app_update esp_partition
)
