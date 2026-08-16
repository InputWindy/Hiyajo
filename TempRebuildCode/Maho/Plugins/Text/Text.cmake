# Text plugin: utfcpp (header-only, self-contained).
maho_set_plugin_output_dirs(${_MOD_TARGET} "${_MOD_PLUGIN_DIR}")

include(FetchContent)

if(NOT TARGET utf8cpp)
	FetchContent_Declare(
		utfcpp
		GIT_REPOSITORY https://github.com/nemtrif/utfcpp.git
		GIT_TAG v4.0.6
		GIT_SHALLOW TRUE
	)
	maho_fetchcontent_populate_or_reuse(utfcpp source/utf8.h)
	add_subdirectory(${utfcpp_SOURCE_DIR} ${utfcpp_BINARY_DIR})
endif()

target_link_libraries(${_MOD_TARGET} PUBLIC utf8cpp)
