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
	"${ENGINE_DIR}/Plugins/Common/Log/Public"
	"${ENGINE_DIR}/Plugins/Common/ConsoleVariable/Public"
)
set_target_properties(EditorConsole PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(EditorConsole PRIVATE MAHO_EDITORCONSOLE_MODULE_EXPORTS)
target_link_libraries(EditorConsole PUBLIC Maho)
set_property(TARGET EditorConsole PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
set_target_properties(EditorConsole PROPERTIES OUTPUT_NAME "FEditorConsole" PREFIX "")
target_link_libraries(EditorConsole PUBLIC ExampleEditor Log ConsoleVariable)
set_target_properties(EditorConsole PROPERTIES FOLDER "Maho/Plugins/GameEngine/EditorCore/EditorUI")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${EditorConsole_PUBLIC_HEADERS} ${EditorConsole_PRIVATE_HEADERS} ${EditorConsole_PRIVATE_SOURCES})
# -- /MAHOGEN EditorConsole --

# EditorConsole plugin: third-party dependencies.
# Dear ImGui (docking branch) is compiled once into a SHARED "maho_imgui" library so
# the editor host (ExampleEditor) and every editor component share ONE process-wide
# ImGui instance over independent CreateContext()/DestroyContext(). The fetch is
# idempotent: if a sibling plugin's cmake already created the target, reuse it.
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
	set_target_properties(maho_imgui PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON FOLDER "ThirdParty")
	set_property(TARGET maho_imgui PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
endif()

target_link_libraries(EditorConsole PUBLIC maho_imgui)
