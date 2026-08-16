# Compress plugin: zstd + zlib (self-contained).
maho_set_plugin_output_dirs(${_MOD_TARGET} "${_MOD_PLUGIN_DIR}")

include(FetchContent)

if(NOT TARGET libzstd_static)
	maho_git_repository_url(_ZSTD_URL https://github.com/facebook/zstd.git)
	FetchContent_Declare(
		zstd
		GIT_REPOSITORY ${_ZSTD_URL}
		GIT_TAG v1.5.6
		GIT_SHALLOW TRUE
	)
	maho_fetchcontent_populate_or_reuse(zstd lib/zstd.h)
	add_subdirectory(${zstd_SOURCE_DIR}/build/cmake ${zstd_BINARY_DIR})
endif()

if(NOT TARGET zlib)
	maho_git_repository_url(_ZLIB_URL https://github.com/madler/zlib.git)
	FetchContent_Declare(
		zlib
		GIT_REPOSITORY ${_ZLIB_URL}
		GIT_TAG v1.3.1
		GIT_SHALLOW TRUE
	)
	maho_fetchcontent_populate_or_reuse(zlib zlib.h)
	add_subdirectory(${zlib_SOURCE_DIR} ${zlib_BINARY_DIR})
endif()

target_link_libraries(${_MOD_TARGET} PUBLIC libzstd_static zlib)
