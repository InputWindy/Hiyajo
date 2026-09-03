# -- MAHOGEN DrawTriangleFeature -- auto-generated build block, do not edit --
file(GLOB DrawTriangleFeature_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB DrawTriangleFeature_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB DrawTriangleFeature_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(DrawTriangleFeature SHARED
${DrawTriangleFeature_PUBLIC_HEADERS}
${DrawTriangleFeature_PRIVATE_HEADERS}
${DrawTriangleFeature_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/DrawTriangleFeature.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/DrawTriangleFeature.cmake"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/DrawTriangleFeature.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/DrawTriangleFeature.cmake" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(DrawTriangleFeature PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
	"${ENGINE_DIR}/Plugins/Engine/GameEngine/RenderCore/Render/Public"
	"${ENGINE_DIR}/Plugins/Engine/GameEngine/RenderCore/RenderFeature/Scene/Public"
)
target_include_directories(DrawTriangleFeature PRIVATE
	"${ENGINE_DIR}/Plugins/Engine/GameEngine/RenderCore/RenderFeature/Frame/Public"
)
set_target_properties(DrawTriangleFeature PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(DrawTriangleFeature PRIVATE MAHO_DRAWTRIANGLEFEATURE_MODULE_EXPORTS)
target_link_libraries(DrawTriangleFeature PUBLIC Maho)
set_property(TARGET DrawTriangleFeature PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
target_link_libraries(DrawTriangleFeature PUBLIC Render Scene)
set_target_properties(DrawTriangleFeature PROPERTIES FOLDER "Maho/Plugins/Engine/GameEngine/RenderCore/RenderFeature")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${DrawTriangleFeature_PUBLIC_HEADERS} ${DrawTriangleFeature_PRIVATE_HEADERS} ${DrawTriangleFeature_PRIVATE_SOURCES})
# -- /MAHOGEN DrawTriangleFeature --

# DrawTriangleFeature plugin: no third-party dependencies.


