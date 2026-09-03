# -- MAHOGEN UIFeature -- auto-generated build block, do not edit --
file(GLOB UIFeature_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB UIFeature_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB UIFeature_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(UIFeature SHARED
${UIFeature_PUBLIC_HEADERS}
${UIFeature_PRIVATE_HEADERS}
${UIFeature_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/UIFeature.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/UIFeature.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/UIFeature.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/UIFeature.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(UIFeature PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
	"${ENGINE_DIR}/Plugins/Engine/GameEngine/RenderCore/Render/Public"
	"${ENGINE_DIR}/Plugins/Engine/GameEngine/RenderCore/RenderFeature/Scene/Public"
)
target_include_directories(UIFeature PRIVATE
	"${ENGINE_DIR}/Plugins/Engine/GameEngine/RenderCore/RenderFeature/DrawTriangleFeature/Public"
	"${ENGINE_DIR}/Plugins/Engine/GameEngine/RenderCore/RenderFeature/Frame/Public"
)
set_target_properties(UIFeature PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(UIFeature PRIVATE MAHO_UIFEATURE_MODULE_EXPORTS)
target_link_libraries(UIFeature PUBLIC Maho)
set_property(TARGET UIFeature PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
target_link_libraries(UIFeature PUBLIC Render Scene)
set_target_properties(UIFeature PROPERTIES FOLDER "Maho/Plugins/Engine/GameEngine/RenderCore/RenderFeature")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${UIFeature_PUBLIC_HEADERS} ${UIFeature_PRIVATE_HEADERS} ${UIFeature_PRIVATE_SOURCES})
# -- /MAHOGEN UIFeature --

# UIFeature: no third-party deps. The ImGui symbols come from Render.dll (which
# compiles Dear ImGui into it); Vulkan/glfw propagate via Render.



