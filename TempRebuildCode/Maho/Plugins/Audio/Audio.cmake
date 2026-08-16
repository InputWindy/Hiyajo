# Audio plugin: miniaudio (single header, self-contained).
maho_set_plugin_output_dirs(${_MOD_TARGET} "${_MOD_PLUGIN_DIR}")

include(FetchContent)

if(NOT DEFINED miniaudio_SOURCE_DIR)
	maho_git_repository_url(_MINIAUDIO_URL https://github.com/mackron/miniaudio.git)
	maho_fetchcontent_populate_or_reuse(miniaudio ${_MINIAUDIO_URL} 0.11.22 miniaudio.h)
endif()

target_include_directories(${_MOD_TARGET} PUBLIC "${miniaudio_SOURCE_DIR}")
