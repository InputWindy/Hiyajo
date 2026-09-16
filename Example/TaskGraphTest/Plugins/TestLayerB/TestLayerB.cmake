# -- MAHOGEN TestLayerB -- auto-generated build block, do not edit --
file(GLOB TestLayerB_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB TestLayerB_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB TestLayerB_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(TestLayerB SHARED
${TestLayerB_PUBLIC_HEADERS}
${TestLayerB_PRIVATE_HEADERS}
${TestLayerB_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/TestLayerB.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/TestLayerB.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/TestLayerB.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/TestLayerB.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(TestLayerB PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/TaskGraphTest/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/TaskGraphTest/Public"
)
set_target_properties(TestLayerB PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(TestLayerB PRIVATE MAHO_TESTLAYERB_MODULE_EXPORTS)
target_link_libraries(TestLayerB PUBLIC Maho)
set_property(TARGET TestLayerB PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
set_target_properties(TestLayerB PROPERTIES OUTPUT_NAME "FTestLayerB" PREFIX "")
set_target_properties(TestLayerB PROPERTIES FOLDER "Project/Plugins")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${TestLayerB_PUBLIC_HEADERS} ${TestLayerB_PRIVATE_HEADERS} ${TestLayerB_PRIVATE_SOURCES})
# -- /MAHOGEN TestLayerB --

# TestLayerB plugin: third-party dependencies.
# The DLL target is built by codegen; this file only pulls FetchContent
# deps and links them into the TestLayerB target.
#
# Example:
#   include(FetchContent)
#   maho_git_repository_url(_URL https://github.com/example/repo.git)
#   maho_fetchcontent_populate_or_reuse(repo ${_URL} v1.0 path/to/marker)
#   maho_add_thirdparty_subdirectory(${repo_SOURCE_DIR} ${repo_BINARY_DIR})
#   target_link_libraries(TestLayerB PUBLIC repo::repo)
