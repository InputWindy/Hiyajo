# Json plugin: nlohmann/json (header-only, self-contained).
# Clone source is configurable via MAHO_GIT_PROXY_PREFIX (see MahoDependencies.cmake).
maho_set_plugin_output_dirs(${_MOD_TARGET} "${_MOD_PLUGIN_DIR}")

include(FetchContent)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)

if(NOT TARGET nlohmann_json::nlohmann_json)
	maho_git_repository_url(_NLJSON_URL https://github.com/nlohmann/json.git)
	FetchContent_Declare(
		nlohmann_json
		GIT_REPOSITORY ${_NLJSON_URL}
		GIT_TAG v3.11.3
		GIT_SHALLOW TRUE
		GIT_PROGRESS TRUE
	)
	maho_fetchcontent_populate_or_reuse(nlohmann_json single_include/nlohmann/json.hpp)
	add_subdirectory(${nlohmann_json_SOURCE_DIR} ${nlohmann_json_BINARY_DIR})
endif()

target_link_libraries(${_MOD_TARGET} PUBLIC nlohmann_json::nlohmann_json)
