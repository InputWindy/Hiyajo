# -- MAHOGEN Paths -- auto-generated build block, do not edit --
file(GLOB Paths_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB Paths_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB Paths_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(Paths SHARED
${Paths_PUBLIC_HEADERS}
${Paths_PRIVATE_HEADERS}
${Paths_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/Paths.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/Paths.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/Paths.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/Paths.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(Paths PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
)
set_target_properties(Paths PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(Paths PRIVATE MAHO_PATHS_MODULE_EXPORTS)
target_link_libraries(Paths PUBLIC Maho)
set_property(TARGET Paths PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
set_target_properties(Paths PROPERTIES FOLDER "Maho/Plugins/Engine/Core")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${Paths_PUBLIC_HEADERS} ${Paths_PRIVATE_HEADERS} ${Paths_PRIVATE_SOURCES})
# -- /MAHOGEN Paths --

# Paths plugin: third-party dependencies.
# None - pure std (filesystem/map/string). The plugin DLL's target is built by
# codegen; this file exists so a self-contained plugin keeps an empty deps file.


