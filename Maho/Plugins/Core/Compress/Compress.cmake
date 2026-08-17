# Compress plugin: zstd + zlib (self-contained).
maho_set_plugin_output_dirs(${_MOD_TARGET} "${_MOD_PLUGIN_DIR}")

include(FetchContent)

if(NOT TARGET libzstd_static)
	maho_git_repository_url(_ZSTD_URL https://github.com/facebook/zstd.git)
	maho_fetchcontent_populate_or_reuse(zstd ${_ZSTD_URL} v1.5.6 lib/zstd.h)
	# Only the static lib is needed; the shared lib's .rc step and the CLI are
	# unneeded (and the .rc step can't find zstd.h on some toolchains).
	set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
	set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
	set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
	add_subdirectory(${zstd_SOURCE_DIR}/build/cmake ${zstd_BINARY_DIR})
endif()

if(NOT TARGET zlib)
	maho_git_repository_url(_ZLIB_URL https://github.com/madler/zlib.git)
	maho_fetchcontent_populate_or_reuse(zlib ${_ZLIB_URL} v1.3.1 zlib.h)
	add_subdirectory(${zlib_SOURCE_DIR} ${zlib_BINARY_DIR})
endif()

target_link_libraries(${_MOD_TARGET} PUBLIC libzstd_static zlib)
