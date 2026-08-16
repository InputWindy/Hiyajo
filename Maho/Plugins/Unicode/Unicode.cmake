# Unicode plugin: utfcpp (header-only, self-contained).
maho_set_plugin_output_dirs(${_MOD_TARGET} "${_MOD_PLUGIN_DIR}")

include(FetchContent)

if(NOT TARGET utf8cpp)
	maho_git_repository_url(_UTF8CPP_URL https://github.com/nemtrif/utfcpp.git)
	maho_fetchcontent_populate_or_reuse(utfcpp ${_UTF8CPP_URL} v4.0.6 source/utf8.h)
	add_subdirectory(${utfcpp_SOURCE_DIR} ${utfcpp_BINARY_DIR})
endif()

target_link_libraries(${_MOD_TARGET} PUBLIC utf8cpp)
