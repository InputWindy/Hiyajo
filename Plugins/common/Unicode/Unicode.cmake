# -- MAHOGEN Unicode -- auto-generated build block, do not edit --
file(GLOB Unicode_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB Unicode_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB Unicode_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(Unicode SHARED
${Unicode_PUBLIC_HEADERS}
${Unicode_PRIVATE_HEADERS}
${Unicode_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/Unicode.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/Unicode.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/Unicode.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/Unicode.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(Unicode PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
)
set_target_properties(Unicode PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(Unicode PRIVATE MAHO_UNICODE_MODULE_EXPORTS)
target_link_libraries(Unicode PUBLIC Maho)
set_property(TARGET Unicode PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
set_target_properties(Unicode PROPERTIES FOLDER "Maho/Plugins/Common")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${Unicode_PUBLIC_HEADERS} ${Unicode_PRIVATE_HEADERS} ${Unicode_PRIVATE_SOURCES})
# -- /MAHOGEN Unicode --

# Unicode plugin: third-party dependencies.
# None - pure std + WinAPI on Windows (string/string_view/cstdint/Windows.h).
# The plugin DLL's target is built by codegen; this file exists so a
# self-contained plugin keeps an empty deps file.
