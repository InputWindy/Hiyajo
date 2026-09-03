# -- MAHOGEN Exception -- auto-generated build block, do not edit --
file(GLOB Exception_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB Exception_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB Exception_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(Exception SHARED
${Exception_PUBLIC_HEADERS}
${Exception_PRIVATE_HEADERS}
${Exception_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/Exception.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/Exception.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/Exception.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/Exception.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(Exception PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
)
set_target_properties(Exception PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(Exception PRIVATE MAHO_EXCEPTION_MODULE_EXPORTS)
target_link_libraries(Exception PUBLIC Maho)
set_property(TARGET Exception PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
set_target_properties(Exception PROPERTIES FOLDER "Maho/Plugins/Engine/Core")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${Exception_PUBLIC_HEADERS} ${Exception_PRIVATE_HEADERS} ${Exception_PRIVATE_SOURCES})
# -- /MAHOGEN Exception --

# Exception plugin: third-party dependencies.
# None - pure std (exception/functional/string/string_view/vector). The plugin
# DLL's target is built by codegen; this file exists so a self-contained plugin
# keeps an empty deps file.
