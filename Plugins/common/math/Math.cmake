# Math plugin: GLM (header-only, self-contained).
include(FetchContent)

if(NOT TARGET glm::glm)
	maho_git_repository_url(_GLM_URL https://github.com/g-truc/glm.git)
	maho_fetchcontent_populate_or_reuse(glm ${_GLM_URL} 0.9.9.8 glm/glm.hpp)
	add_library(glm::glm INTERFACE IMPORTED)
	set_target_properties(glm::glm PROPERTIES
		INTERFACE_INCLUDE_DIRECTORIES "${glm_SOURCE_DIR}"
		INTERFACE_COMPILE_DEFINITIONS "GLM_ENABLE_EXPERIMENTAL"
	)
endif()

target_link_libraries(Math PUBLIC glm::glm)
