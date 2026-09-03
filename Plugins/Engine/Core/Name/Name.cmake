# -- MAHOGEN Name -- auto-generated build block, do not edit --
file(GLOB Name_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB Name_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB Name_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(Name SHARED
${Name_PUBLIC_HEADERS}
${Name_PRIVATE_HEADERS}
${Name_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/Name.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/Name.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/Name.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/Name.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(Name PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
)
set_target_properties(Name PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(Name PRIVATE MAHO_NAME_MODULE_EXPORTS)
target_link_libraries(Name PUBLIC Maho)
set_property(TARGET Name PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
set_target_properties(Name PROPERTIES FOLDER "Maho/Plugins/Engine/Core")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${Name_PUBLIC_HEADERS} ${Name_PRIVATE_HEADERS} ${Name_PRIVATE_SOURCES})
# -- /MAHOGEN Name --

# Name plugin: third-party dependencies.
# None - pure std. Target built by codegen.



