# LVGL comes from the component manager (idf_component.yml). Its configuration is lv_conf.h here, for the board and the
# PC simulator alike: LV_CONF_PATH points LVGL at it and LV_KCONFIG_IGNORE leaves the sdkconfig's LVGL menu out.
# WHOLE_ARCHIVE: the error tags are registered by a constructor in sys_lvgl.c.
idf_component_register(
    SRCS "sys_lvgl.c" "router/sys_lvgl_router.c"
    INCLUDE_DIRS "." "errors" "router"
    REQUIRES errors cmd router sys_device lvgl
    PRIV_REQUIRES utils time log freertos heap sys_display sys_touch
    WHOLE_ARCHIVE
)

idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_definitions(${lvgl_lib} PUBLIC "LV_CONF_PATH=\"${CMAKE_CURRENT_LIST_DIR}/lv_conf.h\"" LV_KCONFIG_IGNORE)
target_include_directories(${lvgl_lib} PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
