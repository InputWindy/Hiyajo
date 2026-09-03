# -- MAHOGEN Config -- auto-generated build block, do not edit --
file(GLOB Config_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB Config_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB Config_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(Config SHARED
${Config_PUBLIC_HEADERS}
${Config_PRIVATE_HEADERS}
${Config_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/Config.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/Config.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/Config.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/Config.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(Config PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
	"${ENGINE_DIR}/Plugins/Common/ConsoleVariable/Public"
)
set_target_properties(Config PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(Config PRIVATE MAHO_CONFIG_MODULE_EXPORTS)
target_link_libraries(Config PUBLIC Maho)
set_property(TARGET Config PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
target_link_libraries(Config PUBLIC ConsoleVariable)
set_target_properties(Config PROPERTIES FOLDER "Maho/Plugins/Engine/Core")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${Config_PUBLIC_HEADERS} ${Config_PRIVATE_HEADERS} ${Config_PRIVATE_SOURCES})
# -- /MAHOGEN Config --

# Config plugin: third-party dependencies.
# None - pure std (cstdint/map/optional/string/string_view/fstream/utility). The
# plugin DLL's target is built by codegen; this file exists so a self-contained
# plugin keeps an empty deps file.
