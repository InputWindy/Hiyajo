# -- MAHOGEN ContentBrowser -- auto-generated build block, do not edit --
file(GLOB ContentBrowser_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB ContentBrowser_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB ContentBrowser_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(ContentBrowser SHARED
${ContentBrowser_PUBLIC_HEADERS}
${ContentBrowser_PRIVATE_HEADERS}
${ContentBrowser_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/ContentBrowser.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/ContentBrowser.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/ContentBrowser.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/ContentBrowser.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(ContentBrowser PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/EditorCore/ExampleEditor/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/EngineCore/UI/Public"
	"${ENGINE_DIR}/Plugins/Common/Paths/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/EngineCore/Asset/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/EngineCore/Resource/Public"
	"${ENGINE_DIR}/Plugins/Common/Log/Public"
)
set_target_properties(ContentBrowser PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(ContentBrowser PRIVATE MAHO_CONTENTBROWSER_MODULE_EXPORTS)
target_link_libraries(ContentBrowser PUBLIC Maho)
set_property(TARGET ContentBrowser PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
set_target_properties(ContentBrowser PROPERTIES OUTPUT_NAME "FContentBrowser" PREFIX "")
target_link_libraries(ContentBrowser PUBLIC ExampleEditor UI Paths Asset Resource Log)
set_target_properties(ContentBrowser PROPERTIES FOLDER "Maho/Plugins/GameEngine/EditorCore/EditorUI")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${ContentBrowser_PUBLIC_HEADERS} ${ContentBrowser_PRIVATE_HEADERS} ${ContentBrowser_PRIVATE_SOURCES})
# -- /MAHOGEN ContentBrowser --

# ContentBrowser plugin: third-party dependencies.
# The DLL target is built by codegen; this file only pulls FetchContent
# deps and links them into the ContentBrowser target.
#
# Example:
#   include(FetchContent)
#   maho_git_repository_url(_URL https://github.com/example/repo.git)
#   maho_fetchcontent_populate_or_reuse(repo ${_URL} v1.0 path/to/marker)
#   maho_add_thirdparty_subdirectory(${repo_SOURCE_DIR} ${repo_BINARY_DIR})
#   target_link_libraries(ContentBrowser PUBLIC repo::repo)

# This panel declares its UI as a component tree through the UI plugin and NEVER calls or
# links ImGui: only the translation layer (UIFeature) speaks the backend, so there is nothing
# third-party to fetch here.
