# Maho core third-party (all FetchContent → build Intermediate).
# Included from Maho/CMakeLists.txt.

# ---------------------------------------------------------------------------
# nlohmann/json — single header (core Json.cpp + Render ShaderCache.cpp).
# ---------------------------------------------------------------------------
FetchContent_Declare(
	nlohmann_json
	GIT_REPOSITORY https://github.com/nlohmann/json.git
	GIT_TAG v3.11.3
	GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)
set(MAHO_NLOHMANN_JSON_INCLUDE_DIR "${nlohmann_json_SOURCE_DIR}/single_include" CACHE INTERNAL "nlohmann/json include root")
message(STATUS "Maho: nlohmann/json (FetchContent) at ${MAHO_NLOHMANN_JSON_INCLUDE_DIR}")

# ---------------------------------------------------------------------------
# GLM (OpenGL Mathematics) — header-only math for ECS transforms / render math.
# ---------------------------------------------------------------------------
FetchContent_Declare(
	glm
	GIT_REPOSITORY https://github.com/g-truc/glm.git
	GIT_TAG 1.0.1
	GIT_SHALLOW TRUE
)
maho_fetchcontent_populate_or_reuse(glm glm/glm.hpp)
set(MAHO_GLM_INCLUDE_DIR "${glm_SOURCE_DIR}" CACHE INTERNAL "glm include directory")
message(STATUS "Maho: glm (FetchContent) at ${MAHO_GLM_INCLUDE_DIR}")
