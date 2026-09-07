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
	"${ENGINE_DIR}/Plugins/GameEngine/RenderCore/Render/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/RenderCore/RenderFeature/Scene/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/GameCore/GameWorld/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/GameCore/SystemGroup/UISystem/Public"
)
target_include_directories(UIFeature PRIVATE
	"${ENGINE_DIR}/Plugins/GameEngine/RenderCore/RenderFeature/DrawTriangleFeature/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/RenderCore/RenderFeature/FrameRenderFeature/Public"
)
set_target_properties(UIFeature PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(UIFeature PRIVATE MAHO_UIFEATURE_MODULE_EXPORTS)
target_link_libraries(UIFeature PUBLIC Maho)
set_property(TARGET UIFeature PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
target_link_libraries(UIFeature PUBLIC Render Scene GameWorld UISystem)
set_target_properties(UIFeature PROPERTIES FOLDER "Maho/Plugins/GameEngine/RenderCore/RenderFeature")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${UIFeature_PUBLIC_HEADERS} ${UIFeature_PRIVATE_HEADERS} ${UIFeature_PRIVATE_SOURCES})
# -- /MAHOGEN UIFeature --

# UIFeature: third-party dependencies.
# Dear ImGui (docking branch) is compiled into a SHARED "maho_imgui" library so
# BOTH the UI render feature (owns the context + drives the frame) and the game-side
# UISystem (submits the UI draw closures to the UIBuilder) link ONE
# process-wide ImGui instance -- a single GImGui shared across the two DLLs. A shared
# lib is REQUIRED: two plugin DLLs each statically linking their own imgui.cpp would
# get separate ImGui state, and the context would crash. The feature still owns the
# ImGui context (created/destroyed in OnInstalled/PreUnInstall) and translates
# ImDrawData into an FDrawList for FRender::AddPass. NO imgui_impl_* backend --
# rendering is the project's custom FRHI backend.
maho_git_repository_url(_IMGUI_URL https://github.com/ocornut/imgui.git)
maho_fetchcontent_populate_or_reuse(imgui ${_IMGUI_URL} v1.91.9-docking imgui.h)
unset(_IMGUI_URL)

if(NOT TARGET maho_imgui)
	add_library(maho_imgui SHARED
		"${imgui_SOURCE_DIR}/imgui.cpp"
		"${imgui_SOURCE_DIR}/imgui_demo.cpp"
		"${imgui_SOURCE_DIR}/imgui_draw.cpp"
		"${imgui_SOURCE_DIR}/imgui_tables.cpp"
		"${imgui_SOURCE_DIR}/imgui_widgets.cpp"
	)
	target_include_directories(maho_imgui PUBLIC "${imgui_SOURCE_DIR}")
	set_target_properties(maho_imgui PROPERTIES
		WINDOWS_EXPORT_ALL_SYMBOLS ON
		FOLDER "ThirdParty"
	)
	set_property(TARGET maho_imgui PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
endif()

# The feature links the ImGui symbols (NewFrame/Render/GetDrawData) and the UI system
# (GetUISystem + GetUIBuilder) that it pulls every frame.
target_link_libraries(UIFeature PUBLIC maho_imgui)

# The feature pulls the game-side UI commands from the UIBuilder: it includes
# <UISystem.h> (which pulls <GameWorld.h>) and links the UISystem + its GameWorld
# dependency.
target_include_directories(UIFeature PUBLIC
	"${ENGINE_DIR}/Plugins/Engine/GameEngine/GameCore/GameWorld/Public"
	"${ENGINE_DIR}/Plugins/Engine/GameEngine/GameCore/SystemGroup/UISystem/Public"
)
target_link_libraries(UIFeature PUBLIC UISystem GameWorld)
