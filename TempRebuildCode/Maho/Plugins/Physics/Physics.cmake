# Physics plugin: Jolt Physics (self-contained).
maho_set_plugin_output_dirs(${_MOD_TARGET} "${_MOD_PLUGIN_DIR}")

include(FetchContent)

if(NOT TARGET Jolt)
	maho_git_repository_url(_JOLT_URL https://github.com/jrouwe/JoltPhysics.git)
	maho_fetchcontent_populate_or_reuse(JoltPhysics ${_JOLT_URL} v5.4.0 Jolt/Jolt.h)
	add_subdirectory(${JoltPhysics_SOURCE_DIR}/Build ${JoltPhysics_BINARY_DIR})
endif()

target_link_libraries(${_MOD_TARGET} PUBLIC Jolt)
