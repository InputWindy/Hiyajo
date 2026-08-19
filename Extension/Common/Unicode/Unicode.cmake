# Unicode plugin: utfcpp (header-only, self-contained).
# The DLL target is built by codegen; this file only pulls FetchContent
# deps and links them into the Unicode target.

include(FetchContent)

if(NOT TARGET utf8cpp)
	maho_git_repository_url(_UTF8CPP_URL https://github.com/nemtrif/utfcpp.git)
	maho_fetchcontent_populate_or_reuse(utfcpp ${_UTF8CPP_URL} v4.0.6 source/utf8.h)
	maho_add_thirdparty_subdirectory(${utfcpp_SOURCE_DIR} ${utfcpp_BINARY_DIR})
endif()

target_link_libraries(Unicode PUBLIC utf8cpp)
