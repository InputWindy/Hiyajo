# -- MAHOGEN EditorOutliner -- auto-generated build block, do not edit --
file(GLOB EditorOutliner_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB EditorOutliner_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB EditorOutliner_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(EditorOutliner SHARED
${EditorOutliner_PUBLIC_HEADERS}
${EditorOutliner_PRIVATE_HEADERS}
${EditorOutliner_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/EditorOutliner.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/EditorOutliner.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/EditorOutliner.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/EditorOutliner.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(EditorOutliner PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/EditorCore/ExampleEditor/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/GameCore/GameWorld/Public"
)
set_target_properties(EditorOutliner PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(EditorOutliner PRIVATE MAHO_EDITOROUTLINER_MODULE_EXPORTS)
target_link_libraries(EditorOutliner PUBLIC Maho)
set_property(TARGET EditorOutliner PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
target_link_libraries(EditorOutliner PUBLIC ExampleEditor GameWorld)
set_target_properties(EditorOutliner PROPERTIES FOLDER "Maho/Plugins/GameEngine/EditorCore/EditorUI")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${EditorOutliner_PUBLIC_HEADERS} ${EditorOutliner_PRIVATE_HEADERS} ${EditorOutliner_PRIVATE_SOURCES})
# -- /MAHOGEN EditorOutliner --

# EditorOutliner plugin: third-party dependencies.
# The DLL target is built by codegen; this file only pulls FetchContent
# deps and links them into the EditorOutliner target.
#
# Example:
#   include(FetchContent)
#   maho_git_repository_url(_URL https://github.com/example/repo.git)
#   maho_fetchcontent_populate_or_reuse(repo ${_URL} v1.0 path/to/marker)
#   maho_add_thirdparty_subdirectory(${repo_SOURCE_DIR} ${repo_BINARY_DIR})
#   target_link_libraries(EditorOutliner PUBLIC repo::repo)

# Dear ImGui (docking branch) is compiled once into a SHARED "maho_imgui" library so the
# game UIFeature, this editor host (ExampleEditor) and every editor component share ONE
# process-wide ImGui instance (a single GImGui) over independent CreateContext()/DestroyContext().
# The fetch is idempotent: if a sibling plugin's cmake already created the target, reuse it.
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

target_link_libraries(EditorOutliner PUBLIC maho_imgui)
