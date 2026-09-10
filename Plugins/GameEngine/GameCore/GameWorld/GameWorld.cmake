# -- MAHOGEN GameWorld -- auto-generated build block, do not edit --
file(GLOB GameWorld_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB GameWorld_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB GameWorld_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(GameWorld SHARED
${GameWorld_PUBLIC_HEADERS}
${GameWorld_PRIVATE_HEADERS}
${GameWorld_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/GameWorld.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/GameWorld.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/GameWorld.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/GameWorld.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(GameWorld PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/EngineCore/Resource/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/EngineCore/Asset/Public"
)
target_include_directories(GameWorld PRIVATE
	"${ENGINE_DIR}/Plugins/GameEngine/GameCore/SystemGroup/UISystem/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/EngineCore/UI/Public"
)
set_target_properties(GameWorld PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(GameWorld PRIVATE MAHO_GAMEWORLD_MODULE_EXPORTS)
target_link_libraries(GameWorld PUBLIC Maho)
set_property(TARGET GameWorld PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
set_target_properties(GameWorld PROPERTIES OUTPUT_NAME "FGameWorld" PREFIX "")
target_link_libraries(GameWorld PUBLIC Resource Asset)
set_target_properties(GameWorld PROPERTIES FOLDER "Maho/Plugins/GameEngine/GameCore")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${GameWorld_PUBLIC_HEADERS} ${GameWorld_PRIVATE_HEADERS} ${GameWorld_PRIVATE_SOURCES})
# -- /MAHOGEN GameWorld --

# GameWorld plugin: third-party dependencies.
# The DLL target is built by codegen; this file only pulls FetchContent
# deps and links them into the GameWorld target.
#
# Example:
#   include(FetchContent)
#   maho_git_repository_url(_URL https://github.com/example/repo.git)
#   maho_fetchcontent_populate_or_reuse(repo ${_URL} v1.0 path/to/marker)
#   maho_add_thirdparty_subdirectory(${repo_SOURCE_DIR} ${repo_BINARY_DIR})
#   target_link_libraries(GameWorld PUBLIC repo::repo)
