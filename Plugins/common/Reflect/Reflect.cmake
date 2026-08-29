# Reflect plugin: refl-cpp - compile-time reflection (header-only, MIT).
# Header-only -> no add_subdirectory; expose an INTERFACE target with the include
# dir so Script / game code can `#include <refl/meta.hpp>` via the transitive link.
include(FetchContent)

maho_git_repository_url(_REFL_URL https://github.com/veselink1/refl-cpp.git)
maho_fetchcontent_populate_or_reuse(reflcpp ${_REFL_URL} v0.12.4 include/refl.hpp)
if(NOT TARGET reflcpp::reflcpp)
	add_library(reflcpp::reflcpp INTERFACE IMPORTED)
	set_target_properties(reflcpp::reflcpp PROPERTIES
		INTERFACE_INCLUDE_DIRECTORIES "${reflcpp_SOURCE_DIR}/include")
endif()

target_link_libraries(Reflect PUBLIC reflcpp::reflcpp)
