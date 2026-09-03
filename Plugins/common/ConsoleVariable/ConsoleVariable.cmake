# -- MAHOGEN ConsoleVariable -- auto-generated build block, do not edit --
file(GLOB ConsoleVariable_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB ConsoleVariable_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB ConsoleVariable_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(ConsoleVariable SHARED
${ConsoleVariable_PUBLIC_HEADERS}
${ConsoleVariable_PRIVATE_HEADERS}
${ConsoleVariable_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/ConsoleVariable.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/ConsoleVariable.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/ConsoleVariable.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/ConsoleVariable.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(ConsoleVariable PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
)
set_target_properties(ConsoleVariable PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(ConsoleVariable PRIVATE MAHO_CONSOLEVARIABLE_MODULE_EXPORTS)
target_link_libraries(ConsoleVariable PUBLIC Maho)
set_property(TARGET ConsoleVariable PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
set_target_properties(ConsoleVariable PROPERTIES FOLDER "Maho/Plugins/Common")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${ConsoleVariable_PUBLIC_HEADERS} ${ConsoleVariable_PRIVATE_HEADERS} ${ConsoleVariable_PRIVATE_SOURCES})
# -- /MAHOGEN ConsoleVariable --

# ConsoleVariable plugin: third-party dependencies.
# None - pure std (cstdint/map/memory/string/string_view/type_traits/utility/
# mutex). The plugin DLL's target is built by codegen; this file exists so a
# self-contained plugin keeps an empty deps file.


