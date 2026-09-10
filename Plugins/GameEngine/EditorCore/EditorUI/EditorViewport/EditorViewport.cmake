# -- MAHOGEN EditorViewport -- auto-generated build block, do not edit --
file(GLOB EditorViewport_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB EditorViewport_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB EditorViewport_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(EditorViewport SHARED
${EditorViewport_PUBLIC_HEADERS}
${EditorViewport_PRIVATE_HEADERS}
${EditorViewport_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/EditorViewport.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/EditorViewport.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/EditorViewport.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/EditorViewport.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(EditorViewport PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/EditorCore/ExampleEditor/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/EngineCore/UI/Public"
	"${ENGINE_DIR}/Plugins/Common/Name/Public"
)
set_target_properties(EditorViewport PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(EditorViewport PRIVATE MAHO_EDITORVIEWPORT_MODULE_EXPORTS)
target_link_libraries(EditorViewport PUBLIC Maho)
set_property(TARGET EditorViewport PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
set_target_properties(EditorViewport PROPERTIES OUTPUT_NAME "FEditorViewport" PREFIX "")
target_link_libraries(EditorViewport PUBLIC ExampleEditor UI Name)
set_target_properties(EditorViewport PROPERTIES FOLDER "Maho/Plugins/GameEngine/EditorCore/EditorUI")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${EditorViewport_PUBLIC_HEADERS} ${EditorViewport_PRIVATE_HEADERS} ${EditorViewport_PRIVATE_SOURCES})
# -- /MAHOGEN EditorViewport --

# EditorViewport plugin: third-party dependencies.
# The DLL target is built by codegen; this file only pulls FetchContent
# deps and links them into the EditorViewport target.
#
# Example:
#   include(FetchContent)
#   maho_git_repository_url(_URL https://github.com/example/repo.git)
#   maho_fetchcontent_populate_or_reuse(repo ${_URL} v1.0 path/to/marker)
#   maho_add_thirdparty_subdirectory(${repo_SOURCE_DIR} ${repo_BINARY_DIR})
#   target_link_libraries(EditorViewport PUBLIC repo::repo)

# This panel declares its UI as a component tree through the UI plugin and NEVER calls or
# links ImGui: only the translation layer (UIFeature) speaks the backend, so there is nothing
# third-party to fetch here.
