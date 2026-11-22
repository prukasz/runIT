include("${CMAKE_CURRENT_LIST_DIR}/../provider.cmake")
runit_provider(
    SRCS "provider_uart.c"
    PRIV_REQUIRES errors sys_data_provider sys_device sys_event sys_uart_contract
)
