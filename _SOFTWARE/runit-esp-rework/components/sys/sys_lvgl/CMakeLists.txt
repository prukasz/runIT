# LVGL comes from the component manager (idf_component.yml). Its configuration is config/ here (lv_conf.h and the headers
# it includes), for the board and the PC simulator alike: LV_CONF_PATH points LVGL at it and LV_KCONFIG_IGNORE leaves the
# sdkconfig's LVGL menu out. LVGL's own sources include config/'s hooks (sys_lvgl_hooks.h).
# WHOLE_ARCHIVE: the error tags are registered by a constructor in sys_lvgl.c.
idf_component_register(
    SRCS "sys_lvgl.c" "router/sys_lvgl_router.c"
    INCLUDE_DIRS "." "errors" "router"
    PRIV_INCLUDE_DIRS "config"
    REQUIRES errors cmd router sys_device sys_event lvgl
    PRIV_REQUIRES utils time log freertos heap sys_display sys_touch
    WHOLE_ARCHIVE
)

idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_definitions(${lvgl_lib} PUBLIC "LV_CONF_PATH=\"${CMAKE_CURRENT_LIST_DIR}/config/lv_conf.h\""
                                              LV_KCONFIG_IGNORE)
target_include_directories(${lvgl_lib} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/config")
