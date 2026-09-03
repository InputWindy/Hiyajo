# -- MAHOGEN Platform -- auto-generated build block, do not edit --
file(GLOB Platform_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB Platform_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB Platform_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(Platform SHARED
${Platform_PUBLIC_HEADERS}
${Platform_PRIVATE_HEADERS}
${Platform_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/Platform.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/Platform.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/Platform.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/Platform.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(Platform PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
	"${ENGINE_DIR}/Plugins/Common/ConsoleVariable/Public"
	"${ENGINE_DIR}/Plugins/Engine/Core/Log/Public"
)
target_include_directories(Platform PRIVATE
	"${ENGINE_DIR}/Plugins/Engine/Core/Config/Public"
)
set_target_properties(Platform PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(Platform PRIVATE MAHO_PLATFORM_MODULE_EXPORTS)
target_link_libraries(Platform PUBLIC Maho)
set_property(TARGET Platform PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
target_link_libraries(Platform PUBLIC ConsoleVariable Log)
set_target_properties(Platform PROPERTIES FOLDER "Maho/Plugins/Engine/GameEngine")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${Platform_PUBLIC_HEADERS} ${Platform_PRIVATE_HEADERS} ${Platform_PRIVATE_SOURCES})
# -- /MAHOGEN Platform --

# Platform plugin: window (GLFW) + headless (EGL) native surfaces.
# The DLL target is built by codegen; this file only pulls GLFW and links it.

# Headless build switch - no window, no GLFW.
#   cmake -DMAHO_HEADLESS=ON ...
if(NOT DEFINED MAHO_HEADLESS AND DEFINED ENV{MAHO_HEADLESS})
	set(MAHO_HEADLESS "$ENV{MAHO_HEADLESS}")
endif()
option(MAHO_HEADLESS "Build headless (no window)" OFF)

if(NOT MAHO_HEADLESS)
	set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
	set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
	set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
	set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

	if(NOT TARGET glfw)
		maho_git_repository_url(_GLFW_URL https://github.com/glfw/glfw.git)
		maho_fetchcontent_populate_or_reuse(glfw ${_GLFW_URL} 3.4 include/GLFW/glfw3.h)
		maho_add_thirdparty_subdirectory(${glfw_SOURCE_DIR} ${glfw_BINARY_DIR})
	endif()

	# Dependents (e.g. a future Render ImGuiSystem) get glfw transitively.
	target_link_libraries(Platform PUBLIC glfw)
else()
	target_compile_definitions(Platform PRIVATE MAHO_HEADLESS=1)
endif()

# Headless backend: EGL (Linux) - available in both modes.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	find_package(EGL REQUIRED)
	target_link_libraries(Platform PUBLIC EGL::EGL)
endif()


