# Json plugin: nlohmann/json (header-only, self-contained).
# The DLL target is built by codegen; this file only pulls FetchContent
# deps and links them into the Json target.

include(FetchContent)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)

if(NOT TARGET nlohmann_json::nlohmann_json)
	maho_git_repository_url(_NLJSON_URL https://github.com/nlohmann/json.git)
	maho_fetchcontent_populate_or_reuse(nlohmann_json ${_NLJSON_URL} v3.11.3 single_include/nlohmann/json.hpp)
	maho_add_thirdparty_subdirectory(${nlohmann_json_SOURCE_DIR} ${nlohmann_json_BINARY_DIR})
endif()

target_link_libraries(Json PUBLIC nlohmann_json::nlohmann_json)
