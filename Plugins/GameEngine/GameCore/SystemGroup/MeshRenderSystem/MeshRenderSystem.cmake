# -- MAHOGEN MeshRenderSystem -- auto-generated build block, do not edit --
file(GLOB MeshRenderSystem_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB MeshRenderSystem_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB MeshRenderSystem_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(MeshRenderSystem SHARED
${MeshRenderSystem_PUBLIC_HEADERS}
${MeshRenderSystem_PRIVATE_HEADERS}
${MeshRenderSystem_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/MeshRenderSystem.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/MeshRenderSystem.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/MeshRenderSystem.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/MeshRenderSystem.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(MeshRenderSystem PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/GameCore/GameWorld/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/RenderCore/RenderFeature/PrimitiveRegistry/Public"
	"${ENGINE_DIR}/Plugins/Common/Name/Public"
	"${ENGINE_DIR}/Plugins/Common/Log/Public"
)
set_target_properties(MeshRenderSystem PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(MeshRenderSystem PRIVATE MAHO_MESHRENDERSYSTEM_MODULE_EXPORTS)
target_link_libraries(MeshRenderSystem PUBLIC Maho)
set_property(TARGET MeshRenderSystem PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
set_target_properties(MeshRenderSystem PROPERTIES OUTPUT_NAME "FMeshRenderSystem" PREFIX "")
target_link_libraries(MeshRenderSystem PUBLIC GameWorld PrimitiveRegistry Name Log)
set_target_properties(MeshRenderSystem PROPERTIES FOLDER "Maho/Plugins/GameEngine/GameCore/SystemGroup")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${MeshRenderSystem_PUBLIC_HEADERS} ${MeshRenderSystem_PRIVATE_HEADERS} ${MeshRenderSystem_PRIVATE_SOURCES})
# -- /MAHOGEN MeshRenderSystem --

# MeshRenderSystem plugin: third-party dependencies.
# The DLL target is built by codegen; this file only pulls FetchContent
# deps and links them into the MeshRenderSystem target.
#
# Example:
#   include(FetchContent)
#   maho_git_repository_url(_URL https://github.com/example/repo.git)
#   maho_fetchcontent_populate_or_reuse(repo ${_URL} v1.0 path/to/marker)
#   maho_add_thirdparty_subdirectory(${repo_SOURCE_DIR} ${repo_BINARY_DIR})
#   target_link_libraries(MeshRenderSystem PUBLIC repo::repo)
