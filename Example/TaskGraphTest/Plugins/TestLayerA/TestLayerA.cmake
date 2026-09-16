# -- MAHOGEN TestLayerA -- auto-generated build block, do not edit --
file(GLOB TestLayerA_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB TestLayerA_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB TestLayerA_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(TestLayerA SHARED
${TestLayerA_PUBLIC_HEADERS}
${TestLayerA_PRIVATE_HEADERS}
${TestLayerA_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/TestLayerA.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/TestLayerA.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/TestLayerA.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/TestLayerA.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(TestLayerA PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/TaskGraphTest/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/TaskGraphTest/Public"
	"${ENGINE_DIR}/Plugins/Common/Config/Public"
	"${ENGINE_DIR}/Plugins/Common/Paths/Public"
	"${ENGINE_DIR}/Plugins/Common/Name/Public"
	"${ENGINE_DIR}/Plugins/Common/Timer/Public"
	"${ENGINE_DIR}/Plugins/Common/Text/Public"
	"${ENGINE_DIR}/Plugins/Common/Exception/Public"
	"${ENGINE_DIR}/Plugins/Common/Log/Public"
)
set_target_properties(TestLayerA PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(TestLayerA PRIVATE MAHO_TESTLAYERA_MODULE_EXPORTS)
target_link_libraries(TestLayerA PUBLIC Maho)
set_property(TARGET TestLayerA PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
set_target_properties(TestLayerA PROPERTIES OUTPUT_NAME "FTestLayerA" PREFIX "")
target_link_libraries(TestLayerA PUBLIC Config Paths Name Timer Text Exception Log)
set_target_properties(TestLayerA PROPERTIES FOLDER "Project/Plugins")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${TestLayerA_PUBLIC_HEADERS} ${TestLayerA_PRIVATE_HEADERS} ${TestLayerA_PRIVATE_SOURCES})
# -- /MAHOGEN TestLayerA --

# TestLayerA plugin: third-party dependencies.
# The DLL target is built by codegen; this file only pulls FetchContent
# deps and links them into the TestLayerA target.
#
# Example:
#   include(FetchContent)
#   maho_git_repository_url(_URL https://github.com/example/repo.git)
#   maho_fetchcontent_populate_or_reuse(repo ${_URL} v1.0 path/to/marker)
#   maho_add_thirdparty_subdirectory(${repo_SOURCE_DIR} ${repo_BINARY_DIR})
#   target_link_libraries(TestLayerA PUBLIC repo::repo)
