file(READ "${CMAKE_CURRENT_LIST_DIR}/pack.json" PACK_JSON)
string(JSON PACK_NAME GET "${PACK_JSON}" name)

if(NOT TARGET rigComponent)
	if(DEFINED RIGKIT_ROOT AND NOT RIGKIT_ROOT STREQUAL "")
		set(_rigComponent_dir "${RIGKIT_ROOT}/packs/rigComponent")
	else()
		set(_rigComponent_dir "${CMAKE_SOURCE_DIR}/packs/rigComponent")
	endif()
	if(EXISTS "${_rigComponent_dir}/CMakeLists.txt")
		add_subdirectory("${_rigComponent_dir}" "${CMAKE_BINARY_DIR}/packs/rigComponent")
	else()
		message(FATAL_ERROR "rigNodeEditor requires rigComponent under packs/rigComponent")
	endif()
endif()

if(NOT TARGET rigProject)
	if(DEFINED RIGKIT_ROOT AND NOT RIGKIT_ROOT STREQUAL "")
		set(_rigProject_dir "${RIGKIT_ROOT}/packs/rigProject")
	else()
		set(_rigProject_dir "${CMAKE_SOURCE_DIR}/packs/rigProject")
	endif()
	if(EXISTS "${_rigProject_dir}/CMakeLists.txt")
		add_subdirectory("${_rigProject_dir}" "${CMAKE_BINARY_DIR}/packs/rigProject")
	else()
		message(FATAL_ERROR "rigNodeEditor requires rigProject under packs/rigProject")
	endif()
endif()

if(NOT TARGET rigNodeComponent)
	if(DEFINED RIGKIT_ROOT AND NOT RIGKIT_ROOT STREQUAL "")
		set(_rigNodeComponent_dir "${RIGKIT_ROOT}/packs/rigNodeComponent")
	else()
		set(_rigNodeComponent_dir "${CMAKE_SOURCE_DIR}/packs/rigNodeComponent")
	endif()
	if(EXISTS "${_rigNodeComponent_dir}/CMakeLists.txt")
		add_subdirectory("${_rigNodeComponent_dir}" "${CMAKE_BINARY_DIR}/packs/rigNodeComponent")
	else()
		message(FATAL_ERROR "rigNodeEditor requires rigNodeComponent under packs/rigNodeComponent")
	endif()
endif()

if(NOT TARGET rigImGui)
	if(DEFINED RIGKIT_ROOT AND NOT RIGKIT_ROOT STREQUAL "")
		set(_rigImGui_dir "${RIGKIT_ROOT}/packs/rigImGui")
	else()
		set(_rigImGui_dir "${CMAKE_SOURCE_DIR}/packs/rigImGui")
	endif()
	if(EXISTS "${_rigImGui_dir}/CMakeLists.txt")
		add_subdirectory("${_rigImGui_dir}" "${CMAKE_BINARY_DIR}/packs/rigImGui")
	else()
		message(FATAL_ERROR "rigNodeEditor requires rigImGui under packs/rigImGui")
	endif()
endif()

add_library(${PACK_NAME} STATIC
	src/rigNodeEditor.cpp
	src/rigNodeEditor.h
	src/NodeEditorWindow.cpp
	src/NodeEditorWindow.h
	src/NodeInspector.cpp
	src/NodeInspector.h
)
target_link_libraries(${PACK_NAME} PUBLIC rigkit rigNodeComponent)
if(TARGET rigProject)
	target_link_libraries(${PACK_NAME} PUBLIC rigProject)
endif()
if(TARGET rigImGui)
	target_link_libraries(${PACK_NAME} PUBLIC rigImGui)
endif()
target_include_directories(${PACK_NAME} PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/src>
)

message(STATUS "Configured pack: ${PACK_NAME}")
