# NVS: the partitions' start and a file of chunks (SYS_NVS.MD).
idf_component_register(
    SRCS "sys_nvs.c"
    INCLUDE_DIRS "." "errors"
    REQUIRES errors nvs_flash
    PRIV_REQUIRES utils log esp_rom
)
