# Config plugin: toml++ (header-only, self-contained).
maho_set_plugin_output_dirs(${_MOD_TARGET} "${_MOD_PLUGIN_DIR}")

include(FetchContent)

if(NOT TARGET tomlplusplus::tomlplusplus)
	FetchContent_Declare(
		tomlplusplus
		GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
		GIT_TAG v3.4.0
		GIT_SHALLOW TRUE
	)
	maho_fetchcontent_populate_or_reuse(tomlplusplus include/toml++/toml.hpp)
	add_subdirectory(${tomlplusplus_SOURCE_DIR} ${tomlplusplus_BINARY_DIR})
endif()

target_link_libraries(${_MOD_TARGET} PUBLIC tomlplusplus::tomlplusplus)
