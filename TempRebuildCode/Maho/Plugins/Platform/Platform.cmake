# Platform plugin: window (GLFW) + headless (EGL) backends.
maho_set_plugin_output_dirs(${_MOD_TARGET} "${_MOD_PLUGIN_DIR}")

# Headless build switch — no window, no GLFW.
#   cmake -DMAHO_HEADLESS=ON ...
#   or  setx MAHO_HEADLESS ON
if(NOT DEFINED MAHO_HEADLESS AND DEFINED ENV{MAHO_HEADLESS})
	set(MAHO_HEADLESS "$ENV{MAHO_HEADLESS}")
endif()
option(MAHO_HEADLESS "Build headless (no window)" OFF)

if(NOT MAHO_HEADLESS)
	# GLFW (windowed) — FetchContent via the streaming clone helper.
	set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
	set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
	set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
	set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

	set(_MAHO_BUILD_SHARED_LIBS_SAVED "${BUILD_SHARED_LIBS}")
	if(MAHO_BUILD_SHARED)
		set(BUILD_SHARED_LIBS ON)
	else()
		set(BUILD_SHARED_LIBS OFF)
	endif()

	if(NOT TARGET glfw)
		maho_git_repository_url(_GLFW_URL https://github.com/glfw/glfw.git)
		maho_fetchcontent_populate_or_reuse(glfw ${_GLFW_URL} 3.4 include/GLFW/glfw3.h)
		add_subdirectory(${glfw_SOURCE_DIR} ${glfw_BINARY_DIR})
	endif()

	set(BUILD_SHARED_LIBS "${_MAHO_BUILD_SHARED_LIBS_SAVED}")
	unset(_MAHO_BUILD_SHARED_LIBS_SAVED)

	# Dependents (Render's ImGuiSystem) get glfw transitively.
	target_link_libraries(${_MOD_TARGET} PUBLIC glfw)
else()
	target_compile_definitions(${_MOD_TARGET} PRIVATE MAHO_HEADLESS=1)
endif()

# Headless backend: EGL (Linux) — available in both modes.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	find_package(EGL REQUIRED)
	target_link_libraries(${_MOD_TARGET} PUBLIC EGL::EGL)
endif()
