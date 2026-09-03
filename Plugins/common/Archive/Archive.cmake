# -- MAHOGEN Archive -- auto-generated build block, do not edit --
file(GLOB Archive_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB Archive_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB Archive_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(Archive SHARED
${Archive_PUBLIC_HEADERS}
${Archive_PRIVATE_HEADERS}
${Archive_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/Archive.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/Archive.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/Archive.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/Archive.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(Archive PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
)
set_target_properties(Archive PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(Archive PRIVATE MAHO_ARCHIVE_MODULE_EXPORTS)
target_link_libraries(Archive PUBLIC Maho)
set_property(TARGET Archive PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
set_target_properties(Archive PROPERTIES FOLDER "Maho/Plugins/Common")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${Archive_PUBLIC_HEADERS} ${Archive_PRIVATE_HEADERS} ${Archive_PRIVATE_SOURCES})
# -- /MAHOGEN Archive --

# Archive plugin: third-party dependencies.
# None - pure std (cstddef/cstdint/string/type_traits/vector/cstring). The plugin
# DLL's target is built by codegen; this file exists so a self-contained plugin
# keeps an empty deps file.
