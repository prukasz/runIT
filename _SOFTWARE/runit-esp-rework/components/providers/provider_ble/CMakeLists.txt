include("${CMAKE_CURRENT_LIST_DIR}/../provider.cmake")
runit_provider(
    SRCS "provider_ble.c"
    PRIV_REQUIRES cmd errors sys_data_provider sys_ble
)
