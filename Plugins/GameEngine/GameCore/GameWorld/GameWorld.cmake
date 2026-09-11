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
# Building GameWorld alone must also build the sub-plugins it installs at
# runtime (UISystem) - otherwise a sub-plugin DLL left over from a
# previous build is silently installed. A POST_BUILD script action, NOT
# add_dependencies(GameWorld, <sub>): a sub-plugin links GameWorld, so that edge
# would close a target cycle and CMake refuses to generate (cycles are
# allowed only among static libraries).
# GameWorld_SubPlugins is a plain handle (nothing links or depends on it) that
# names every sub-plugin in ONE nested build: one msbuild invocation per
# target would rebuild the shared dependency chain once per sub-plugin.
# It carries no sources and no output of its own, so it is parked under
# ThirdParty/CodeGen — a code-gen artifact of GameWorld, not a plugin of it.
add_custom_target(GameWorld_SubPlugins)
add_dependencies(GameWorld_SubPlugins UISystem)
set_target_properties(GameWorld_SubPlugins PROPERTIES FOLDER "ThirdParty/CodeGen")
add_custom_command(TARGET GameWorld POST_BUILD
	COMMAND "${CMAKE_COMMAND}"
		"-DMAHO_BUILD_DIR=${CMAKE_BINARY_DIR}"
		"-DMAHO_CONFIG=$<CONFIG>"
		"-DMAHO_TARGETS=GameWorld_SubPlugins"
		"-DMAHO_IN_SOLUTION_BUILD=$(BuildingSolutionFile)"
		"-DMAHO_SUBPLUGIN_BUILD=$(MahoSubPluginBuild)"
		-P "${ENGINE_DIR}/Tools/build_subplugins.cmake"
	COMMENT "GameWorld: ensuring enabled sub-plugins are up to date (UISystem)"
	VERBATIM
)
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
