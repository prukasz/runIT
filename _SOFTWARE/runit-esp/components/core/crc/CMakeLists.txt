# The board's CRCs (CRC.MD): crc.h, this chip's implementation in esp/ (the ROM's).
idf_component_register(
    SRCS "esp/crc_esp.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_rom
)
