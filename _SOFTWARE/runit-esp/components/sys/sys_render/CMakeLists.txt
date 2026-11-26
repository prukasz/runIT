# The render adapter (sys_render.h) with its LVGL driver (lvgl_impl) and memory (esp_malloc_impl).
# ".." for "sys_render/..." includes. LVGL comes from the component manager (idf_component.yml);
# LV_CONF_PATH points LVGL at lvgl_impl/lv_conf.h and LV_KCONFIG_IGNORE leaves sdkconfig's LVGL menu out.
# WHOLE_ARCHIVE: error tags and router self-register via constructors.
idf_component_register(
    SRCS "sys_render.c" "router/sys_render_router.c" "lvgl_impl/sys_render_lvgl.c" "esp_malloc_impl/board_mem_esp.c"
    INCLUDE_DIRS "." "errors" "config" "router" ".."
    PRIV_INCLUDE_DIRS "internal" "lvgl_impl"
    REQUIRES errors cmd router sys_device sys_event sys_display_contract utils
    PRIV_REQUIRES lvgl heap time freertos
    WHOLE_ARCHIVE
)

idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_definitions(${lvgl_lib} PUBLIC "LV_CONF_PATH=\"${CMAKE_CURRENT_LIST_DIR}/lvgl_impl/lv_conf.h\""
                                              LV_KCONFIG_IGNORE)
target_include_directories(${lvgl_lib} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/lvgl_impl")
