# Math plugin: GLM (header-only, self-contained).
maho_set_plugin_output_dirs(${_MOD_TARGET} "${_MOD_PLUGIN_DIR}")

include(FetchContent)

if(NOT TARGET glm::glm)
	FetchContent_Declare(
		glm
		GIT_REPOSITORY https://github.com/g-truc/glm.git
		GIT_TAG 1.0.1
		GIT_SHALLOW TRUE
	)
	maho_fetchcontent_populate_or_reuse(glm glm/glm.hpp)
	add_subdirectory(${glm_SOURCE_DIR} ${glm_BINARY_DIR})
endif()

target_link_libraries(${_MOD_TARGET} PUBLIC glm::glm)
