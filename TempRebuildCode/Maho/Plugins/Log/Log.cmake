# Log plugin: spdlog (header-only, self-contained).
maho_set_plugin_output_dirs(${_MOD_TARGET} "${_MOD_PLUGIN_DIR}")

include(FetchContent)

set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)

if(NOT TARGET spdlog::spdlog_header_only)
	maho_git_repository_url(_SPDLOG_URL https://github.com/gabime/spdlog.git)
	FetchContent_Declare(
		spdlog
		GIT_REPOSITORY ${_SPDLOG_URL}
		GIT_TAG v1.15.3
		GIT_SHALLOW TRUE
	)
	maho_fetchcontent_populate_or_reuse(spdlog include/spdlog/spdlog.h)
	add_subdirectory(${spdlog_SOURCE_DIR} ${spdlog_BINARY_DIR})
endif()

target_link_libraries(${_MOD_TARGET} PUBLIC spdlog::spdlog_header_only)
