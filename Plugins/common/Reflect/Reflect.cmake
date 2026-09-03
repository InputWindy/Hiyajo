# -- MAHOGEN Reflect -- auto-generated build block, do not edit --
file(GLOB Reflect_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB Reflect_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB Reflect_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(Reflect SHARED
${Reflect_PUBLIC_HEADERS}
${Reflect_PRIVATE_HEADERS}
${Reflect_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/Reflect.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/Reflect.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/Reflect.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/Reflect.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(Reflect PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
)
set_target_properties(Reflect PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(Reflect PRIVATE MAHO_REFLECT_MODULE_EXPORTS)
target_link_libraries(Reflect PUBLIC Maho)
set_property(TARGET Reflect PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
set_target_properties(Reflect PROPERTIES FOLDER "Maho/Plugins/Common")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${Reflect_PUBLIC_HEADERS} ${Reflect_PRIVATE_HEADERS} ${Reflect_PRIVATE_SOURCES})
# -- /MAHOGEN Reflect --

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


