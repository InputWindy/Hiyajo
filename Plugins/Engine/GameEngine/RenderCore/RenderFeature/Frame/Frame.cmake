# -- MAHOGEN Frame -- auto-generated build block, do not edit --
file(GLOB Frame_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB Frame_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB Frame_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(Frame SHARED
${Frame_PUBLIC_HEADERS}
${Frame_PRIVATE_HEADERS}
${Frame_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/Frame.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/Frame.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/Frame.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/Frame.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(Frame PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
	"${ENGINE_DIR}/Plugins/Engine/GameEngine/RenderCore/Render/Public"
	"${ENGINE_DIR}/Plugins/Engine/GameEngine/RenderCore/RenderFeature/Scene/Public"
)
target_include_directories(Frame PRIVATE
	"${ENGINE_DIR}/Plugins/Engine/GameEngine/RenderCore/RenderFeature/DrawTriangleFeature/Public"
	"${ENGINE_DIR}/Plugins/Engine/GameEngine/RenderCore/RenderFeature/UIFeature/Public"
)
set_target_properties(Frame PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(Frame PRIVATE MAHO_FRAME_MODULE_EXPORTS)
target_link_libraries(Frame PUBLIC Maho)
set_property(TARGET Frame PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
target_link_libraries(Frame PUBLIC Render Scene)
set_target_properties(Frame PROPERTIES FOLDER "Maho/Plugins/Engine/GameEngine/RenderCore/RenderFeature")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${Frame_PUBLIC_HEADERS} ${Frame_PRIVATE_HEADERS} ${Frame_PRIVATE_SOURCES})
# -- /MAHOGEN Frame --

# Frame plugin: no third-party dependencies.



