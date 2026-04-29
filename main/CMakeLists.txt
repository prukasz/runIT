set(srcs
    "main.c"
    "../components/drivers_manager/manager_ble/manager_ble.c"
    "../components/drivers_abstraction/a_ble_driver/a_ble.c"
    "../components/drivers_abstraction/a_ble_driver/gap.c"
    "../components/drivers_abstraction/a_ble_driver/gatt_svc.c"
)

idf_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS
        "."
        "../components/drivers_manager/manager_ble/include"
        "../components/drivers_abstraction/a_ble_driver/include"
        "../components/status/include"
    REQUIRES bt nvs_flash
)

                    