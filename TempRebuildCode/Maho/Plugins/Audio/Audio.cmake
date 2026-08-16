# Audio plugin: miniaudio (single header, self-contained).
maho_set_plugin_output_dirs(${_MOD_TARGET} "${_MOD_PLUGIN_DIR}")

include(FetchContent)

if(NOT DEFINED miniaudio_SOURCE_DIR)
	FetchContent_Declare(
		miniaudio
		GIT_REPOSITORY https://github.com/mackron/miniaudio.git
		GIT_TAG 0.11.22
		GIT_SHALLOW TRUE
	)
	maho_fetchcontent_populate_or_reuse(miniaudio miniaudio.h)
endif()

target_include_directories(${_MOD_TARGET} PUBLIC "${miniaudio_SOURCE_DIR}")
