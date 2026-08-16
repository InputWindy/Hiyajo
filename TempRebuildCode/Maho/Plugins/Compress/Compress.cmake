# Compress plugin: zstd + zlib (self-contained).
maho_set_plugin_output_dirs(${_MOD_TARGET} "${_MOD_PLUGIN_DIR}")

include(FetchContent)

if(NOT TARGET libzstd_static)
	FetchContent_Declare(
		zstd
		GIT_REPOSITORY https://github.com/facebook/zstd.git
		GIT_TAG v1.5.6
		GIT_SHALLOW TRUE
	)
	maho_fetchcontent_populate_or_reuse(zstd lib/zstd.h)
	add_subdirectory(${zstd_SOURCE_DIR}/build/cmake ${zstd_BINARY_DIR})
endif()

if(NOT TARGET zlib)
	FetchContent_Declare(
		zlib
		GIT_REPOSITORY https://github.com/madler/zlib.git
		GIT_TAG v1.3.1
		GIT_SHALLOW TRUE
	)
	maho_fetchcontent_populate_or_reuse(zlib zlib.h)
	add_subdirectory(${zlib_SOURCE_DIR} ${zlib_BINARY_DIR})
endif()

target_link_libraries(${_MOD_TARGET} PUBLIC libzstd_static zlib)
