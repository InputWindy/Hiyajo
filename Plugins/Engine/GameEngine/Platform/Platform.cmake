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
