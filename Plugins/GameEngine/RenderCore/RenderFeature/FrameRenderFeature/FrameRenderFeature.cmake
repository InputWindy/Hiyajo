# -- MAHOGEN FrameRenderFeature -- auto-generated build block, do not edit --
file(GLOB FrameRenderFeature_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB FrameRenderFeature_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB FrameRenderFeature_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(FrameRenderFeature SHARED
${FrameRenderFeature_PUBLIC_HEADERS}
${FrameRenderFeature_PRIVATE_HEADERS}
${FrameRenderFeature_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/FrameRenderFeature.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/FrameRenderFeature.cmake"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/FrameRenderFeature.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/FrameRenderFeature.cmake" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(FrameRenderFeature PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/RenderCore/Render/Public"
)
set_target_properties(FrameRenderFeature PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(FrameRenderFeature PRIVATE MAHO_FRAMERENDERFEATURE_MODULE_EXPORTS)
target_link_libraries(FrameRenderFeature PUBLIC Maho)
set_property(TARGET FrameRenderFeature PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
set_target_properties(FrameRenderFeature PROPERTIES OUTPUT_NAME "FFrameRenderFeature" PREFIX "")
target_link_libraries(FrameRenderFeature PUBLIC Render)
set_target_properties(FrameRenderFeature PROPERTIES FOLDER "Maho/Plugins/GameEngine/RenderCore/RenderFeature")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${FrameRenderFeature_PUBLIC_HEADERS} ${FrameRenderFeature_PRIVATE_HEADERS} ${FrameRenderFeature_PRIVATE_SOURCES})
# -- /MAHOGEN FrameRenderFeature --
