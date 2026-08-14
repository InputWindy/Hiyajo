# Platform plugin: owns the window platform library (GLFW).
maho_set_plugin_output_dirs(${_MOD_TARGET} "${_MOD_PLUGIN_DIR}")

# ---------------------------------------------------------------------------
# GLFW (window / input / time) — FetchContent.
# ---------------------------------------------------------------------------
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

FetchContent_Declare(
	glfw
	GIT_REPOSITORY https://github.com/glfw/glfw.git
	GIT_TAG 3.4
	GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(glfw)

set(BUILD_SHARED_LIBS "${_MAHO_BUILD_SHARED_LIBS_SAVED}")
unset(_MAHO_BUILD_SHARED_LIBS_SAVED)

if(TARGET glfw)
	set_target_properties(glfw PROPERTIES FOLDER "Plugins/Platform")
endif()

# Dependents (imgui's glfw backend, Render's ImGuiSystem) get glfw transitively.
target_link_libraries(${_MOD_TARGET} PUBLIC glfw)
