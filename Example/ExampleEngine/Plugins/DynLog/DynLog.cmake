# DynLog plugin: third-party dependencies.
# The DLL target is built by codegen; this file only pulls FetchContent
# deps and links them into the DynLog target.
#
# Example:
#   include(FetchContent)
#   maho_git_repository_url(_URL https://github.com/example/repo.git)
#   maho_fetchcontent_populate_or_reuse(repo ${_URL} v1.0 path/to/marker)
#   maho_add_thirdparty_subdirectory(${repo_SOURCE_DIR} ${repo_BINARY_DIR})
#   target_link_libraries(DynLog PUBLIC repo::repo)
