# -- MAHOGEN Text -- auto-generated build block, do not edit --
file(GLOB Text_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB Text_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB Text_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(Text SHARED
${Text_PUBLIC_HEADERS}
${Text_PRIVATE_HEADERS}
${Text_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/Text.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/Text.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/Text.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/Text.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(Text PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
)
set_target_properties(Text PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(Text PRIVATE MAHO_TEXT_MODULE_EXPORTS)
target_link_libraries(Text PUBLIC Maho)
set_property(TARGET Text PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
set_target_properties(Text PROPERTIES FOLDER "Maho/Plugins/Engine/Core")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${Text_PUBLIC_HEADERS} ${Text_PRIVATE_HEADERS} ${Text_PRIVATE_SOURCES})
# -- /MAHOGEN Text --

# Text plugin: third-party dependencies.
# nlohmann/json (header-only) is an ENGINE-level dep - fetched once in
# Build/CMake/MahoDependencies.cmake and linked PUBLIC into Maho
# (nlohmann_json::nlohmann_json), so this plugin gets <nlohmann/json.hpp>
# transitively via Maho. No fetch here; the DLL target is built by codegen.


