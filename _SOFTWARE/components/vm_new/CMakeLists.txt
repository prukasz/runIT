# Collect all C sources under this component (recursively).
file(GLOB_RECURSE VM_ENGINE_SRCS "${CMAKE_CURRENT_LIST_DIR}/*.c")

# Collect all headers and derive include directories automatically.
file(GLOB_RECURSE VM_ENGINE_HDRS "${CMAKE_CURRENT_LIST_DIR}/*.h")

set(VM_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}")
foreach(_hdr IN LISTS VM_ENGINE_HDRS)
	get_filename_component(_hdir "${_hdr}" DIRECTORY)
	file(RELATIVE_PATH _rel "${CMAKE_CURRENT_LIST_DIR}" "${_hdir}")
	if(_rel STREQUAL "")
		list(APPEND VM_INCLUDE_DIRS ".")
	else()
		list(APPEND VM_INCLUDE_DIRS "${_rel}")
	endif()
endforeach()

list(REMOVE_DUPLICATES VM_INCLUDE_DIRS)

idf_component_register(
	SRCS ${VM_ENGINE_SRCS}
	INCLUDE_DIRS ${VM_INCLUDE_DIRS}
	PRIV_REQUIRES utils status runit_kernel manager_power manager_io
)
