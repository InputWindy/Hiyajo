# -- MAHOGEN Compress -- auto-generated build block, do not edit --
file(GLOB Compress_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB Compress_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB Compress_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(Compress SHARED
${Compress_PUBLIC_HEADERS}
${Compress_PRIVATE_HEADERS}
${Compress_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/Compress.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/Compress.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/Compress.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/Compress.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(Compress PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
)
set_target_properties(Compress PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(Compress PRIVATE MAHO_COMPRESS_MODULE_EXPORTS)
target_link_libraries(Compress PUBLIC Maho)
set_property(TARGET Compress PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
set_target_properties(Compress PROPERTIES FOLDER "Maho/Plugins/Common")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${Compress_PUBLIC_HEADERS} ${Compress_PRIVATE_HEADERS} ${Compress_PRIVATE_SOURCES})
# -- /MAHOGEN Compress --

# Compress plugin: third-party dependencies.
# zstd is an ENGINE-level dep - fetched once in Build/CMake/MahoDependencies.cmake
# and linked PUBLIC into Maho (libzstd_static), so this plugin gets <zstd.h> +
# the static lib transitively via Maho. No fetch here; the DLL target is built by
# codegen.



