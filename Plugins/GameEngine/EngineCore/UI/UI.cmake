# -- MAHOGEN UI -- auto-generated build block, do not edit --
file(GLOB UI_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB UI_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB UI_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(UI SHARED
${UI_PUBLIC_HEADERS}
${UI_PRIVATE_HEADERS}
${UI_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/UI.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/UI.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/UI.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/UI.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(UI PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
	"${ENGINE_DIR}/Plugins/Common/Name/Public"
	"${ENGINE_DIR}/Plugins/Common/Log/Public"
)
set_target_properties(UI PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(UI PRIVATE MAHO_UI_MODULE_EXPORTS)
target_link_libraries(UI PUBLIC Maho)
set_property(TARGET UI PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
set_target_properties(UI PROPERTIES OUTPUT_NAME "FUIViewRegistry" PREFIX "")
target_link_libraries(UI PUBLIC Name Log)
set_target_properties(UI PROPERTIES FOLDER "Maho/Plugins/GameEngine/EngineCore")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${UI_PUBLIC_HEADERS} ${UI_PRIVATE_HEADERS} ${UI_PRIVATE_SOURCES})
# -- /MAHOGEN UI --

# UI plugin: third-party dependencies.
# The DLL target is built by codegen; this file only pulls FetchContent
# deps and links them into the UI target.
#
# Example:
#   include(FetchContent)
#   maho_git_repository_url(_URL https://github.com/example/repo.git)
#   maho_fetchcontent_populate_or_reuse(repo ${_URL} v1.0 path/to/marker)
#   maho_add_thirdparty_subdirectory(${repo_SOURCE_DIR} ${repo_BINARY_DIR})
#   target_link_libraries(UI PUBLIC repo::repo)

# Dear ImGui (docking branch) compiled into a SHARED "maho_imgui" library, so every
# DLL that touches ImGui (the translator here, the render feature, the editor panels)
# links ONE process-wide GImGui. A shared lib is REQUIRED: two plugin DLLs each
# statically linking their own imgui.cpp would get separate ImGui state and the
# context would crash. Only this plugin's Private/ files may include <imgui.h>.
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

# The translator (ImGuiTranslator.cpp) calls into ImGui every frame.
target_link_libraries(UI PUBLIC maho_imgui)
