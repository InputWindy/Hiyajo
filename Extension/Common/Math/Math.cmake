# Math plugin: GLM (header-only, self-contained).
# The DLL target is built by codegen; this file only pulls FetchContent
# deps and links them into the Math target.

include(FetchContent)

if(NOT TARGET glm::glm)
	maho_git_repository_url(_GLM_URL https://github.com/g-truc/glm.git)
	maho_fetchcontent_populate_or_reuse(glm ${_GLM_URL} 1.0.1 glm/glm.hpp)
	maho_add_thirdparty_subdirectory(${glm_SOURCE_DIR} ${glm_BINARY_DIR})
endif()

target_link_libraries(Math PUBLIC glm::glm)
