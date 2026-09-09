# -- MAHOGEN Scene -- auto-generated build block, do not edit --
file(GLOB Scene_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB Scene_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB Scene_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(Scene SHARED
${Scene_PUBLIC_HEADERS}
${Scene_PRIVATE_HEADERS}
${Scene_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/Scene.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/Scene.cmake"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/Scene.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/Scene.cmake" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(Scene PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/RenderCore/Render/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/EngineCore/Resource/Public"
)
target_include_directories(Scene PRIVATE
	"${ENGINE_DIR}/Plugins/GameEngine/EngineCore/Asset/Public"
)
set_target_properties(Scene PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(Scene PRIVATE MAHO_SCENE_MODULE_EXPORTS)
target_link_libraries(Scene PUBLIC Maho)
set_property(TARGET Scene PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
set_target_properties(Scene PROPERTIES OUTPUT_NAME "FScene" PREFIX "")
target_link_libraries(Scene PUBLIC Render Resource)
set_target_properties(Scene PROPERTIES FOLDER "Maho/Plugins/GameEngine/RenderCore/RenderFeature")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${Scene_PUBLIC_HEADERS} ${Scene_PRIVATE_HEADERS} ${Scene_PRIVATE_SOURCES})
# -- /MAHOGEN Scene --

# Scene plugin: no third-party dependencies.
# DLL output is set by codegen (MAHOGEN block) to the layer type "FScene" per the
# module naming protocol: MAHO_DECLARE_LAYER(FScene) + platform suffix.
