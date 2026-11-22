# The render adapter (sys_render.h) with its LVGL driver (lvgl_impl) and memory (esp_malloc_impl).
# ".." for "sys_render/..." includes. LV_CONF_INCLUDE_SIMPLE: lv_conf.h from lvgl_impl.
idf_component_register(
    SRCS "sys_render.c" "lvgl_impl/sys_render_lvgl.c" "esp_malloc_impl/board_mem_esp.c"
    INCLUDE_DIRS "." "errors" "config" "router" ".."
    PRIV_INCLUDE_DIRS "internal" "lvgl_impl"
    REQUIRES errors cmd sys_device sys_event sys_display utils
    PRIV_REQUIRES lvgl heap
)
