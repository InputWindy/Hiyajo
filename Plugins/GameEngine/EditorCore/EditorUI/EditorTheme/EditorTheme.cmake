# -- MAHOGEN EditorTheme -- auto-generated build block, do not edit --
file(GLOB EditorTheme_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB EditorTheme_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB EditorTheme_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(EditorTheme SHARED
${EditorTheme_PUBLIC_HEADERS}
${EditorTheme_PRIVATE_HEADERS}
${EditorTheme_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/EditorTheme.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/EditorTheme.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/EditorTheme.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/EditorTheme.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(EditorTheme PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/EditorCore/ExampleEditor/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/EngineCore/UI/Public"
)
set_target_properties(EditorTheme PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(EditorTheme PRIVATE MAHO_EDITORTHEME_MODULE_EXPORTS)
target_link_libraries(EditorTheme PUBLIC Maho)
set_property(TARGET EditorTheme PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
set_target_properties(EditorTheme PROPERTIES OUTPUT_NAME "FEditorTheme" PREFIX "")
target_link_libraries(EditorTheme PUBLIC ExampleEditor UI)
set_target_properties(EditorTheme PROPERTIES FOLDER "Maho/Plugins/GameEngine/EditorCore/EditorUI")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${EditorTheme_PUBLIC_HEADERS} ${EditorTheme_PRIVATE_HEADERS} ${EditorTheme_PRIVATE_SOURCES})
# -- /MAHOGEN EditorTheme --

# EditorTheme plugin: third-party dependencies.
# The DLL target is built by codegen; this file only pulls FetchContent
# deps and links them into the EditorTheme target.
#
# Example:
#   include(FetchContent)
#   maho_git_repository_url(_URL https://github.com/example/repo.git)
#   maho_fetchcontent_populate_or_reuse(repo ${_URL} v1.0 path/to/marker)
#   maho_add_thirdparty_subdirectory(${repo_SOURCE_DIR} ${repo_BINARY_DIR})
#   target_link_libraries(EditorTheme PUBLIC repo::repo)
