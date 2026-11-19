# The board's identity (BOARD_ID.MD): its id in the store, its boot id, its build; this chip's part in esp/.
idf_component_register(
    SRCS "board_id.c" "esp/board_id_esp.c"
    INCLUDE_DIRS "."
    PRIV_INCLUDE_DIRS "internal"
    REQUIRES errors
    PRIV_REQUIRES cmd nvs_store esp_app_format esp_hw_support
)
