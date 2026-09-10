# -- MAHOGEN EditorConsole -- auto-generated build block, do not edit --
file(GLOB EditorConsole_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB EditorConsole_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB EditorConsole_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(EditorConsole SHARED
${EditorConsole_PUBLIC_HEADERS}
${EditorConsole_PRIVATE_HEADERS}
${EditorConsole_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/EditorConsole.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/EditorConsole.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/EditorConsole.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/EditorConsole.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(EditorConsole PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/EditorCore/ExampleEditor/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/EngineCore/UI/Public"
	"${ENGINE_DIR}/Plugins/Common/Log/Public"
	"${ENGINE_DIR}/Plugins/Common/ConsoleVariable/Public"
)
set_target_properties(EditorConsole PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(EditorConsole PRIVATE MAHO_EDITORCONSOLE_MODULE_EXPORTS)
target_link_libraries(EditorConsole PUBLIC Maho)
set_property(TARGET EditorConsole PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
set_target_properties(EditorConsole PROPERTIES OUTPUT_NAME "FEditorConsole" PREFIX "")
target_link_libraries(EditorConsole PUBLIC ExampleEditor UI Log ConsoleVariable)
set_target_properties(EditorConsole PROPERTIES FOLDER "Maho/Plugins/GameEngine/EditorCore/EditorUI")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${EditorConsole_PUBLIC_HEADERS} ${EditorConsole_PRIVATE_HEADERS} ${EditorConsole_PRIVATE_SOURCES})
# -- /MAHOGEN EditorConsole --

# This panel declares its UI as a component tree through the UI plugin and NEVER calls or
# links ImGui: only the translation layer (UIFeature) speaks the backend, so there is nothing
# third-party to fetch here.
