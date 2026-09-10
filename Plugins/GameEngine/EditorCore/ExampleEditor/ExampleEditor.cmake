# -- MAHOGEN ExampleEditor -- auto-generated build block, do not edit --
file(GLOB ExampleEditor_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB ExampleEditor_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB ExampleEditor_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(ExampleEditor SHARED
${ExampleEditor_PUBLIC_HEADERS}
${ExampleEditor_PRIVATE_HEADERS}
${ExampleEditor_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/ExampleEditor.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/ExampleEditor.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/ExampleEditor.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/ExampleEditor.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(ExampleEditor PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/RenderCore/Render/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/RenderCore/RenderFeature/Scene/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/EngineCore/UI/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/RenderCore/RenderFeature/UIFeature/Public"
)
target_include_directories(ExampleEditor PRIVATE
	"${ENGINE_DIR}/Plugins/GameEngine/RenderCore/RenderFeature/FrameRenderFeature/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/RenderCore/RenderFeature/UIFeature/Public"
)
set_target_properties(ExampleEditor PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(ExampleEditor PRIVATE MAHO_EXAMPLEEDITOR_MODULE_EXPORTS)
target_link_libraries(ExampleEditor PUBLIC Maho)
set_property(TARGET ExampleEditor PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
set_target_properties(ExampleEditor PROPERTIES OUTPUT_NAME "FExampleEditor" PREFIX "")
target_link_libraries(ExampleEditor PUBLIC Render Scene UI UIFeature)
# Building ExampleEditor alone must also build the sub-plugins it installs at
# runtime (EditorViewport EditorConsole ContentBrowser EditorTheme) - otherwise a sub-plugin DLL left over from a
# previous build is silently installed. A POST_BUILD script action, NOT
# add_dependencies(ExampleEditor, <sub>): a sub-plugin links ExampleEditor, so that edge
# would close a target cycle and CMake refuses to generate (cycles are
# allowed only among static libraries).
# ExampleEditor_SubPlugins is a plain handle (nothing links or depends on it) that
# names every sub-plugin in ONE nested build: one msbuild invocation per
# target would rebuild the shared dependency chain once per sub-plugin.
# It carries no sources and no output of its own, so it is parked under
# ThirdParty/CodeGen — a code-gen artifact of ExampleEditor, not a plugin of it.
add_custom_target(ExampleEditor_SubPlugins)
add_dependencies(ExampleEditor_SubPlugins EditorViewport EditorConsole ContentBrowser EditorTheme)
set_target_properties(ExampleEditor_SubPlugins PROPERTIES FOLDER "ThirdParty/CodeGen")
add_custom_command(TARGET ExampleEditor POST_BUILD
	COMMAND "${CMAKE_COMMAND}"
		"-DMAHO_BUILD_DIR=${CMAKE_BINARY_DIR}"
		"-DMAHO_CONFIG=$<CONFIG>"
		"-DMAHO_TARGETS=ExampleEditor_SubPlugins"
		"-DMAHO_IN_SOLUTION_BUILD=$(BuildingSolutionFile)"
		"-DMAHO_SUBPLUGIN_BUILD=$(MahoSubPluginBuild)"
		-P "${ENGINE_DIR}/Tools/build_subplugins.cmake"
	COMMENT "ExampleEditor: ensuring enabled sub-plugins are up to date (EditorViewport EditorConsole ContentBrowser EditorTheme)"
	VERBATIM
)
set_target_properties(ExampleEditor PROPERTIES FOLDER "Maho/Plugins/GameEngine/EditorCore")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${ExampleEditor_PUBLIC_HEADERS} ${ExampleEditor_PRIVATE_HEADERS} ${ExampleEditor_PRIVATE_SOURCES})
# -- /MAHOGEN ExampleEditor --

# ExampleEditor plugin: third-party dependencies.
# Dear ImGui (docking branch) is compiled into a SHARED "maho_imgui" library so BOTH the
# game UIFeature and this editor OWN independent ImGui contexts over ONE process-wide imgui
# instance -- a single GImGui shared across the two DLLs. That is REQUIRED: two plugin DLLs
# each statically linking their own imgui.cpp get separate ImGui state, and the contexts
# would crash. Each feature still owns its own ImGui::CreateContext()/DestroyContext() and
# switches SetCurrentContext per frame.
#
# The fetch is idempotent: if UIFeature's cmake already ran (both build together in an
# editor build), the "maho_imgui" target + imgui_SOURCE_DIR already exist and we reuse them.
	maho_git_repository_url(_IMGUI_URL https://github.com/ocornut/imgui.git)
	maho_fetchcontent_populate_or_reuse(imgui ${_IMGUI_URL} v1.91.9-docking imgui.h)

	# The editor drives ImGui with a hand-fed Win32 input + a self-drawn shader (same model
	# as FUIFeature) -- it does NOT use the ImGui GLFW backend. There is therefore no
	# process-wide ImGui_ImplGlfw single-instance and no clash with any GLFW callbacks, so
	# no imgui_impl_glfw.cpp and no glfw link are needed here.
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

# The editor links the ImGui symbols + the Render/Scene feature plugins it calls into.
# Scene is compile-visible via Dependencies here (GetScene symbol); the render surface the
# editor owns is created through FRender (Render is a Dependencies link). Cross-feature type
# references (UIView.h from the UI plugin, FrameRenderFeature.h for the reverse BlockOn edge)
# come from the codegen PrivateIncludes include paths.
# The editor is the docking host and the only layer that drives ImGui's frame for its OWN
# context; the plugin links it explicitly. Render/Scene/UI/UIFeature come from the generated
# block above (codegen from Dependencies).
target_link_libraries(ExampleEditor PUBLIC maho_imgui)
