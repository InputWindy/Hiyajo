# -- MAHOGEN Timer -- auto-generated build block, do not edit --
file(GLOB Timer_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB Timer_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB Timer_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(Timer SHARED
${Timer_PUBLIC_HEADERS}
${Timer_PRIVATE_HEADERS}
${Timer_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/Timer.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/Timer.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/Timer.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/Timer.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(Timer PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
)
set_target_properties(Timer PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(Timer PRIVATE MAHO_TIMER_MODULE_EXPORTS)
target_link_libraries(Timer PUBLIC Maho)
set_property(TARGET Timer PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
set_target_properties(Timer PROPERTIES FOLDER "Maho/Plugins/Engine/Core")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${Timer_PUBLIC_HEADERS} ${Timer_PRIVATE_HEADERS} ${Timer_PRIVATE_SOURCES})
# -- /MAHOGEN Timer --

# Timer plugin: third-party dependencies.
# None - pure std (chrono/cstdint/map/string/string_view/algorithm/functional/
# sstream). The plugin DLL's target is built by codegen; this file exists so a
# self-contained plugin keeps an empty deps file.



