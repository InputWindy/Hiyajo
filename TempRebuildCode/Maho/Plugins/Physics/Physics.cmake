# Physics plugin: Jolt Physics (self-contained).
maho_set_plugin_output_dirs(${_MOD_TARGET} "${_MOD_PLUGIN_DIR}")

include(FetchContent)

if(NOT TARGET Jolt)
	maho_git_repository_url(_JOLT_URL https://github.com/jrouwe/JoltPhysics.git)
	FetchContent_Declare(
		JoltPhysics
		GIT_REPOSITORY ${_JOLT_URL}
		GIT_TAG v5.4.0
		GIT_SHALLOW TRUE
	)
	maho_fetchcontent_populate_or_reuse(JoltPhysics Jolt/Jolt.h)
	add_subdirectory(${JoltPhysics_SOURCE_DIR}/Build ${JoltPhysics_BINARY_DIR})
endif()

target_link_libraries(${_MOD_TARGET} PUBLIC Jolt)
