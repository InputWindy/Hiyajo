# Json plugin: nlohmann/json (header-only, self-contained).
include(FetchContent)

if(NOT TARGET nlohmann_json::nlohmann_json)
	maho_git_repository_url(_NLJSON_URL https://github.com/nlohmann/json.git)
	maho_fetchcontent_populate_or_reuse(nlohmann_json ${_NLJSON_URL} v3.11.3 single_include/nlohmann/json.hpp)
	add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
	set_target_properties(nlohmann_json::nlohmann_json PROPERTIES
		INTERFACE_INCLUDE_DIRECTORIES "${nlohmann_json_SOURCE_DIR}/single_include")
endif()

target_link_libraries(Json PUBLIC nlohmann_json::nlohmann_json)
