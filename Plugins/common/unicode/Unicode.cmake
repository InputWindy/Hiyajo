# Unicode plugin: utf8cpp (header-only).
include(FetchContent)

if(NOT TARGET utf8cpp::utf8cpp)
	maho_git_repository_url(_UTF8_URL https://github.com/nemtrif/utfcpp.git)
	maho_fetchcontent_populate_or_reuse(utf8cpp ${_UTF8_URL} v3.2.4 source/utf8.h)
	add_library(utf8cpp::utf8cpp INTERFACE IMPORTED)
	set_target_properties(utf8cpp::utf8cpp PROPERTIES
		INTERFACE_INCLUDE_DIRECTORIES "${utf8cpp_SOURCE_DIR}/source")
endif()

target_link_libraries(Unicode PUBLIC utf8cpp::utf8cpp)
