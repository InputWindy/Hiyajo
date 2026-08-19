# Log plugin: spdlog (header-only, self-contained).
# The DLL target is built by codegen; this file only pulls FetchContent
# deps and links them into the Log target.

include(FetchContent)

set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)

if(NOT TARGET spdlog::spdlog_header_only)
	maho_git_repository_url(_SPDLOG_URL https://github.com/gabime/spdlog.git)
	maho_fetchcontent_populate_or_reuse(spdlog ${_SPDLOG_URL} v1.15.3 include/spdlog/spdlog.h)
	maho_add_thirdparty_subdirectory(${spdlog_SOURCE_DIR} ${spdlog_BINARY_DIR})
endif()

target_link_libraries(Log PUBLIC spdlog::spdlog_header_only)
