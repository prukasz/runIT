# NVS: the partitions' start and a file of chunks (SYS_NVS.MD).
# WHOLE_ARCHIVE: the error tags are registered by a constructor in errors/sys_nvs_errors.c, which nothing else
# references: the linker would drop the registration.
idf_component_register(
    SRCS "sys_nvs.c" "errors/sys_nvs_errors.c"
    INCLUDE_DIRS "." "errors"
    REQUIRES errors nvs_flash
    PRIV_REQUIRES utils log esp_rom
    WHOLE_ARCHIVE
)
