# -- MAHOGEN MeshFeature -- auto-generated build block, do not edit --
file(GLOB MeshFeature_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB MeshFeature_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB MeshFeature_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(MeshFeature SHARED
${MeshFeature_PUBLIC_HEADERS}
${MeshFeature_PRIVATE_HEADERS}
${MeshFeature_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/MeshFeature.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/MeshFeature.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/MeshFeature.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/MeshFeature.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(MeshFeature PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/RenderCore/Render/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/RenderCore/RenderFeature/Scene/Public"
	"${ENGINE_DIR}/Plugins/Common/Log/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/GameCore/GameWorld/Public"
)
set_target_properties(MeshFeature PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(MeshFeature PRIVATE MAHO_MESHFEATURE_MODULE_EXPORTS)
target_link_libraries(MeshFeature PUBLIC Maho)
set_property(TARGET MeshFeature PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
set_target_properties(MeshFeature PROPERTIES OUTPUT_NAME "FMeshFeature" PREFIX "")
target_link_libraries(MeshFeature PUBLIC Render Scene Log GameWorld)
set_target_properties(MeshFeature PROPERTIES FOLDER "Maho/Plugins/GameEngine/RenderCore/RenderFeature")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${MeshFeature_PUBLIC_HEADERS} ${MeshFeature_PRIVATE_HEADERS} ${MeshFeature_PRIVATE_SOURCES})
# -- /MAHOGEN MeshFeature --

# MeshFeature plugin: third-party dependencies.
# The DLL target is built by codegen; this file only pulls FetchContent
# deps and links them into the MeshFeature target.
#
# Example:
#   include(FetchContent)
#   maho_git_repository_url(_URL https://github.com/example/repo.git)
#   maho_fetchcontent_populate_or_reuse(repo ${_URL} v1.0 path/to/marker)
#   maho_add_thirdparty_subdirectory(${repo_SOURCE_DIR} ${repo_BINARY_DIR})
#   target_link_libraries(MeshFeature PUBLIC repo::repo)
