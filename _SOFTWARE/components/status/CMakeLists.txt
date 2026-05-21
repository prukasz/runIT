
file(GLOB code_items RELATIVE ${CMAKE_CURRENT_LIST_DIR} "codes/*")
set(code_dirs "")
foreach(item ${code_items})
    if(IS_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/${item}")
        list(APPEND code_dirs "${item}")
    endif()
endforeach()

idf_component_register(
    SRCS "status.c" "codes/status_codes.c"
    INCLUDE_DIRS "include" ${code_dirs} "codes"
    REQUIRES esp_ringbuf
    PRIV_REQUIRES utils
)