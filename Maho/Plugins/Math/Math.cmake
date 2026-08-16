# Math plugin: GLM (header-only, self-contained).
maho_set_plugin_output_dirs(${_MOD_TARGET} "${_MOD_PLUGIN_DIR}")

include(FetchContent)

if(NOT TARGET glm::glm)
	maho_git_repository_url(_GLM_URL https://github.com/g-truc/glm.git)
	maho_fetchcontent_populate_or_reuse(glm ${_GLM_URL} 1.0.1 glm/glm.hpp)
	add_subdirectory(${glm_SOURCE_DIR} ${glm_BINARY_DIR})
endif()

target_link_libraries(${_MOD_TARGET} PUBLIC glm::glm)
