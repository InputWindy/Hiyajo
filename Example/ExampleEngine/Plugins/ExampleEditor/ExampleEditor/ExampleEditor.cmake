# -- MAHOGEN ExampleEditor -- auto-generated build block, do not edit --
file(GLOB ExampleEditor_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB ExampleEditor_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB ExampleEditor_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(ExampleEditor SHARED
${ExampleEditor_PUBLIC_HEADERS}
${ExampleEditor_PRIVATE_HEADERS}
${ExampleEditor_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/ExampleEditor.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/ExampleEditor.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/ExampleEditor.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/ExampleEditor.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(ExampleEditor PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
	"${ENGINE_DIR}/Plugins/Engine/GameEngine/RenderCore/Render/Public"
)
set_target_properties(ExampleEditor PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(ExampleEditor PRIVATE MAHO_EXAMPLEEDITOR_MODULE_EXPORTS)
target_link_libraries(ExampleEditor PUBLIC Maho)
set_property(TARGET ExampleEditor PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
target_link_libraries(ExampleEditor PUBLIC Render)
set_target_properties(ExampleEditor PROPERTIES FOLDER "Project/Plugins/ExampleEditor")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${ExampleEditor_PUBLIC_HEADERS} ${ExampleEditor_PRIVATE_HEADERS} ${ExampleEditor_PRIVATE_SOURCES})
# -- /MAHOGEN ExampleEditor --

# ExampleEditor plugin: third-party dependencies.
# The DLL target is built by codegen; this file only pulls FetchContent
# deps and links them into the ExampleEditor target.
#
# Example:
#   include(FetchContent)
#   maho_git_repository_url(_URL https://github.com/example/repo.git)
#   maho_fetchcontent_populate_or_reuse(repo ${_URL} v1.0 path/to/marker)
#   maho_add_thirdparty_subdirectory(${repo_SOURCE_DIR} ${repo_BINARY_DIR})
#   target_link_libraries(ExampleEditor PUBLIC repo::repo)


