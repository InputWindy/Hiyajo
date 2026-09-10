# -- MAHOGEN UISystem -- auto-generated build block, do not edit --
file(GLOB UISystem_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB UISystem_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB UISystem_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(UISystem SHARED
${UISystem_PUBLIC_HEADERS}
${UISystem_PRIVATE_HEADERS}
${UISystem_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/UISystem.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/UISystem.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/UISystem.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/UISystem.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(UISystem PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/GameCore/GameWorld/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/EngineCore/UI/Public"
)
set_target_properties(UISystem PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(UISystem PRIVATE MAHO_UISYSTEM_MODULE_EXPORTS)
target_link_libraries(UISystem PUBLIC Maho)
set_property(TARGET UISystem PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
set_target_properties(UISystem PROPERTIES OUTPUT_NAME "FUISystem" PREFIX "")
target_link_libraries(UISystem PUBLIC GameWorld UI)
set_target_properties(UISystem PROPERTIES FOLDER "Maho/Plugins/GameEngine/GameCore/SystemGroup")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${UISystem_PUBLIC_HEADERS} ${UISystem_PRIVATE_HEADERS} ${UISystem_PRIVATE_SOURCES})
# -- /MAHOGEN UISystem --

# UISystem owns the GAME-side UI: it declares a persistent view tree per widget entity
# and registers the view in the UI plugin's registry (MAHO_UI). It therefore NEVER
# links or includes ImGui -- only the translation layer (UIFeature) speaks the backend.
# The game context is fetched through UI::GetUIGameRenderContext() (published by
# UIFeature), so UISystem does not depend on UIFeature either: the game never
# perceives Render.
