# Math plugin: GLM (header-only, self-contained).
# The DLL target is built by codegen; this file only pulls FetchContent
# deps and links them into the Math target.

include(FetchContent)

# glm is header-only — fetch the source tree and add its include dir only.
if(NOT DEFINED glm_SOURCE_DIR)
	maho_git_repository_url(_GLM_URL https://github.com/g-truc/glm.git)
	maho_fetchcontent_populate_or_reuse(glm ${_GLM_URL} 1.0.0 glm/glm.hpp)
endif()

target_include_directories(Math PUBLIC "${glm_SOURCE_DIR}")

set_property(TARGET Math PROPERTY CXX_STANDARD 17)
set_property(TARGET Math PROPERTY CXX_STANDARD_REQUIRED ON)
