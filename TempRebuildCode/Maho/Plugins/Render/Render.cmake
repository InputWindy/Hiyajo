# Render plugin: VMA + glslang + Dear ImGui are owned here. imgui and its
# extensions compile into Render.dll, not Maho.dll; Render re-exports their
# PUBLIC include dirs / compile defs so game/editor TUs can use them.
maho_set_plugin_output_dirs(${_MOD_TARGET} "${_MOD_PLUGIN_DIR}")

# ---------------------------------------------------------------------------
# Dear ImGui + extensions via FetchContent (do not vendor these trees in git).
# ---------------------------------------------------------------------------
FetchContent_Declare(
	imgui
	GIT_REPOSITORY https://github.com/ocornut/imgui.git
	GIT_TAG v1.91.9-docking
	GIT_SHALLOW TRUE
)
maho_fetchcontent_populate_or_reuse(imgui imgui.cpp)
set(MAHO_IMGUI_SOURCE_DIR "${imgui_SOURCE_DIR}" CACHE INTERNAL "Dear ImGui source directory" FORCE)
message(STATUS "Maho: ImGui (FetchContent) at ${MAHO_IMGUI_SOURCE_DIR}")

# Flush-left dock tabs without clearing FramePadding (needed for centered tab labels).
# Also skip WindowBorderSize inset so enabling window borders does not shift tabs 1px.
# Idempotent: skips if already patched.
set(_MAHO_IMGUI_CPP "${MAHO_IMGUI_SOURCE_DIR}/imgui.cpp")
if(EXISTS "${_MAHO_IMGUI_CPP}")
	file(READ "${_MAHO_IMGUI_CPP}" _MAHO_IMGUI_CONTENTS)
	if(NOT _MAHO_IMGUI_CONTENTS MATCHES "Maho: dock tab bar flush-left")
		string(REPLACE
			"float button_sz = g.FontSize;\n    r.Min.x += style.FramePadding.x;\n    r.Max.x -= style.FramePadding.x;\n    ImVec2 window_menu_button_pos"
			"float button_sz = g.FontSize;\n    // Maho: dock tab bar flush-left — keep FramePadding for TabItem label centering.\n    // Upstream insets the whole bar by FramePadding.x, which prevents flush-left tabs.\n    // r.Min.x += style.FramePadding.x;\n    // r.Max.x -= style.FramePadding.x;\n    ImVec2 window_menu_button_pos"
			_MAHO_IMGUI_CONTENTS "${_MAHO_IMGUI_CONTENTS}")
		if(_MAHO_IMGUI_CONTENTS MATCHES "Maho: dock tab bar flush-left")
			file(WRITE "${_MAHO_IMGUI_CPP}" "${_MAHO_IMGUI_CONTENTS}")
			message(STATUS "Maho: patched ImGui DockNodeCalcTabBarLayout for flush-left tabs")
		else()
			message(WARNING "Maho: failed to patch ImGui dock tab-bar FramePadding inset (source layout changed?)")
		endif()
	endif()
	file(READ "${_MAHO_IMGUI_CPP}" _MAHO_IMGUI_CONTENTS)
	if(NOT _MAHO_IMGUI_CONTENTS MATCHES "Maho: do not inset tab bar by WindowBorderSize")
		string(REPLACE
			"if (out_title_rect) { *out_title_rect = r; }\n\n    r.Min.x += style.WindowBorderSize;\n    r.Max.x -= style.WindowBorderSize;\n\n    float button_sz = g.FontSize;"
			"if (out_title_rect) { *out_title_rect = r; }\n\n    // Maho: do not inset tab bar by WindowBorderSize (keeps tabs flush with panel edge when borders are on).\n    // r.Min.x += style.WindowBorderSize;\n    // r.Max.x -= style.WindowBorderSize;\n\n    float button_sz = g.FontSize;"
			_MAHO_IMGUI_CONTENTS "${_MAHO_IMGUI_CONTENTS}")
		if(_MAHO_IMGUI_CONTENTS MATCHES "Maho: do not inset tab bar by WindowBorderSize")
			file(WRITE "${_MAHO_IMGUI_CPP}" "${_MAHO_IMGUI_CONTENTS}")
			message(STATUS "Maho: patched ImGui DockNodeCalcTabBarLayout WindowBorderSize inset")
		else()
			message(WARNING "Maho: failed to patch ImGui dock tab-bar WindowBorderSize inset (source layout changed?)")
		endif()
	endif()
endif()
unset(_MAHO_IMGUI_CPP)
unset(_MAHO_IMGUI_CONTENTS)

# ImGuizmo master (v1.9+) lays sources under src/; pin master + use that subdir.
FetchContent_Declare(
	imguizmo
	GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
	GIT_TAG master
	GIT_SHALLOW TRUE
)
maho_fetchcontent_populate_or_reuse(imguizmo src/ImGuizmo.cpp ImGuizmo.cpp)
if(EXISTS "${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp")
	set(MAHO_IMGUIZMO_SOURCE_DIR "${imguizmo_SOURCE_DIR}/src" CACHE INTERNAL "ImGuizmo source directory" FORCE)
elseif(EXISTS "${imguizmo_SOURCE_DIR}/ImGuizmo.cpp")
	set(MAHO_IMGUIZMO_SOURCE_DIR "${imguizmo_SOURCE_DIR}" CACHE INTERNAL "ImGuizmo source directory" FORCE)
else()
	message(FATAL_ERROR "Maho: ImGuizmo sources not found under ${imguizmo_SOURCE_DIR}")
endif()

FetchContent_Declare(
	imgui_node_editor
	GIT_REPOSITORY https://github.com/thedmd/imgui-node-editor.git
	# v0.9.3 predates ImGui 1.90+ (ImVec2 ops / GetKeyIndex removal); develop tracks current ImGui.
	GIT_TAG develop
	GIT_SHALLOW TRUE
)
maho_fetchcontent_populate_or_reuse(imgui_node_editor imgui_node_editor.cpp)
set(MAHO_IMGUI_NODE_EDITOR_SOURCE_DIR "${imgui_node_editor_SOURCE_DIR}" CACHE INTERNAL "imgui-node-editor source directory" FORCE)

FetchContent_Declare(
	implot
	GIT_REPOSITORY https://github.com/epezent/implot.git
	GIT_TAG v0.16
	GIT_SHALLOW TRUE
)
maho_fetchcontent_populate_or_reuse(implot implot.cpp)
set(MAHO_IMPLOT_SOURCE_DIR "${implot_SOURCE_DIR}" CACHE INTERNAL "ImPlot source directory" FORCE)

FetchContent_Declare(
	imgui_file_dialog
	GIT_REPOSITORY https://github.com/aiekick/ImGuiFileDialog.git
	GIT_TAG v0.6.7
	GIT_SHALLOW TRUE
)
maho_fetchcontent_populate_or_reuse(imgui_file_dialog ImGuiFileDialog.cpp)
set(MAHO_IMGUI_FILE_DIALOG_SOURCE_DIR "${imgui_file_dialog_SOURCE_DIR}" CACHE INTERNAL "ImGuiFileDialog source directory" FORCE)

FetchContent_Declare(
	icon_font_cpp_headers
	GIT_REPOSITORY https://github.com/juliettef/IconFontCppHeaders.git
	GIT_TAG main
	GIT_SHALLOW TRUE
)
maho_fetchcontent_populate_or_reuse(icon_font_cpp_headers IconsFontAwesome6.h)
set(MAHO_ICON_FONT_HEADERS_DIR "${icon_font_cpp_headers_SOURCE_DIR}" CACHE INTERNAL "IconFontCppHeaders include directory" FORCE)

message(STATUS "Maho: ImGuizmo            ${MAHO_IMGUIZMO_SOURCE_DIR}")
message(STATUS "Maho: imgui-node-editor   ${MAHO_IMGUI_NODE_EDITOR_SOURCE_DIR}")
message(STATUS "Maho: ImPlot              ${MAHO_IMPLOT_SOURCE_DIR}")
message(STATUS "Maho: ImGuiFileDialog     ${MAHO_IMGUI_FILE_DIALOG_SOURCE_DIR}")
message(STATUS "Maho: IconFontCppHeaders  ${MAHO_ICON_FONT_HEADERS_DIR}")

set(MAHO_IMGUI_SOURCES
	"${MAHO_IMGUI_SOURCE_DIR}/imgui.cpp"
	"${MAHO_IMGUI_SOURCE_DIR}/imgui_demo.cpp"
	"${MAHO_IMGUI_SOURCE_DIR}/imgui_draw.cpp"
	"${MAHO_IMGUI_SOURCE_DIR}/imgui_tables.cpp"
	"${MAHO_IMGUI_SOURCE_DIR}/imgui_widgets.cpp"
	"${MAHO_IMGUI_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
	"${MAHO_IMGUI_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp"
)

set(MAHO_IMGUI_EXT_SOURCES
	"${MAHO_IMGUIZMO_SOURCE_DIR}/ImGuizmo.cpp"
	"${MAHO_IMGUIZMO_SOURCE_DIR}/ImCurveEdit.cpp"
	"${MAHO_IMGUIZMO_SOURCE_DIR}/ImGradient.cpp"
	"${MAHO_IMGUI_NODE_EDITOR_SOURCE_DIR}/imgui_node_editor.cpp"
	"${MAHO_IMGUI_NODE_EDITOR_SOURCE_DIR}/imgui_node_editor_api.cpp"
	"${MAHO_IMGUI_NODE_EDITOR_SOURCE_DIR}/imgui_canvas.cpp"
	"${MAHO_IMGUI_NODE_EDITOR_SOURCE_DIR}/crude_json.cpp"
	"${MAHO_IMPLOT_SOURCE_DIR}/implot.cpp"
	"${MAHO_IMPLOT_SOURCE_DIR}/implot_items.cpp"
	"${MAHO_IMPLOT_SOURCE_DIR}/implot_demo.cpp"
	"${MAHO_IMGUI_FILE_DIALOG_SOURCE_DIR}/ImGuiFileDialog.cpp"
)

# STATIC helper lib (own .vcxproj under ThirdParty/). Avoid OBJECT — VS would
# nest imgui*.obj under Render's "Object Libraries" filter.
add_library(imgui STATIC ${MAHO_IMGUI_SOURCES} ${MAHO_IMGUI_EXT_SOURCES})
add_library(Maho::ImGui ALIAS imgui)

target_include_directories(imgui
	PUBLIC
		"${MAHO_IMGUI_SOURCE_DIR}"
		"${MAHO_IMGUI_SOURCE_DIR}/backends"
		# IMGUI_USER_CONFIG resolves to UI/ImGuiConfig.h under the Render plugin
		# Public root (Render.cmake is included with _MOD_SOURCE_DIR set), and
		# that config includes Core/Export.h from Maho's public headers.
		"${_MOD_SOURCE_DIR}/Public"
		"${MAHO_PUBLIC_DIR}"
		"${MAHO_IMGUIZMO_SOURCE_DIR}"
		"${MAHO_IMGUI_NODE_EDITOR_SOURCE_DIR}"
		"${MAHO_IMPLOT_SOURCE_DIR}"
		"${MAHO_IMGUI_FILE_DIALOG_SOURCE_DIR}"
		"${MAHO_ICON_FONT_HEADERS_DIR}"
)

target_compile_definitions(imgui
	PUBLIC
		IMGUI_USER_CONFIG="UI/ImGuiConfig.h"
		MAHO_WITH_IMGUI=1
		MAHO_WITH_IMGUI_EXTENSIONS=1
		USE_IMGUI_API=1
		USE_STD_FILESYSTEM=1
	PRIVATE
		$<$<BOOL:${MAHO_BUILD_SHARED}>:MAHO_BUILD_SHARED=1>
		$<$<BOOL:${MAHO_BUILD_SHARED}>:MAHO_EXPORTS=1>
)

target_link_libraries(imgui
	PUBLIC
		Vulkan::Vulkan
	PRIVATE
		Platform
)

target_compile_features(imgui PUBLIC cxx_std_20)

if(MSVC)
	target_compile_options(imgui PRIVATE /W4 /permissive- /Zc:preprocessor /utf-8)
	set_source_files_properties(${MAHO_IMGUI_EXT_SOURCES} PROPERTIES COMPILE_FLAGS "/W3")
endif()

set_target_properties(imgui PROPERTIES
	FOLDER "Plugins/Render"
	POSITION_INDEPENDENT_CODE ON
)

source_group(TREE "${MAHO_IMGUI_SOURCE_DIR}" PREFIX "imgui" FILES ${MAHO_IMGUI_SOURCES})
source_group("imgui_ext" FILES ${MAHO_IMGUI_EXT_SOURCES})
list(LENGTH MAHO_IMGUI_EXT_SOURCES _MAHO_IMGUI_EXT_COUNT)
message(STATUS "Maho: ImGui extensions: ${_MAHO_IMGUI_EXT_COUNT} source file(s)")
unset(_MAHO_IMGUI_EXT_COUNT)

# Render.dll consumes every imgui TU; on MSVC shared builds WHOLEARCHIVE pulls in
# demo/tables/widgets that the linker would otherwise drop. Re-export imgui's
# PUBLIC include dirs + compile defs so game/editor TUs can #include <imgui.h>
# and see MAHO_WITH_IMGUI against Render.dll.
add_dependencies(${_MOD_TARGET} imgui)
if(MAHO_BUILD_SHARED)
	if(MSVC)
		target_link_options(${_MOD_TARGET} PRIVATE "/WHOLEARCHIVE:$<TARGET_FILE:imgui>")
	else()
		target_link_libraries(${_MOD_TARGET} PRIVATE
			"-Wl,--whole-archive"
			imgui
			"-Wl,--no-whole-archive"
		)
	endif()
else()
	target_link_libraries(${_MOD_TARGET} PRIVATE imgui)
endif()

target_include_directories(${_MOD_TARGET} PUBLIC
	$<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>
)
target_compile_definitions(${_MOD_TARGET} PUBLIC
	$<TARGET_PROPERTY:imgui,INTERFACE_COMPILE_DEFINITIONS>
)

if(MSVC AND MAHO_BUILD_SHARED)
	# maho_add_plugin_modules sets WINDOWS_EXPORT_ALL_SYMBOLS OFF for plugins;
	# Render must export imgui symbols so the editor EXE can call them directly.
	set_target_properties(${_MOD_TARGET} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

# ---------------------------------------------------------------------------
# Vulkan Memory Allocator (header-only) — FetchContent.
# ---------------------------------------------------------------------------
FetchContent_Declare(
	vma
	GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
	GIT_TAG v3.2.1
	GIT_SHALLOW TRUE
)
maho_fetchcontent_populate_or_reuse(vma include/vk_mem_alloc.h)
set(_RENDER_VMA_INCLUDE_DIR "${vma_SOURCE_DIR}/include")
message(STATUS "Maho: Render VMA (FetchContent) at ${_RENDER_VMA_INCLUDE_DIR}")
target_include_directories(${_MOD_TARGET} PRIVATE "${_RENDER_VMA_INCLUDE_DIR}")

if(WIN32)
	target_compile_definitions(${_MOD_TARGET} PRIVATE VK_USE_PLATFORM_WIN32_KHR=1)
endif()

# nlohmann/json stays central (Maho core Json.cpp also uses it), but Render TUs
# still need the include path directly because Maho keeps it PRIVATE.
if(DEFINED MAHO_NLOHMANN_JSON_INCLUDE_DIR)
	target_include_directories(${_MOD_TARGET} PRIVATE "${MAHO_NLOHMANN_JSON_INCLUDE_DIR}")
endif()

# ---------------------------------------------------------------------------
# glslang — Khronos GLSL → SPIR-V compiler (runtime shader compilation).
# ---------------------------------------------------------------------------
option(MAHO_FETCH_GLSLANG "FetchContent glslang from GitHub" ON)
set(MAHO_GLSLANG_SOURCE_DIR "" CACHE PATH "Pre-cloned glslang root (optional)")
set(MAHO_GLSLANG_BINARY_DIR "" CACHE PATH "glslang build dir (optional, paired with SOURCE_DIR)")
set(_RENDER_HAS_GLSLANG 0)

set(_RENDER_GLSLANG_SRC "")
if(MAHO_GLSLANG_SOURCE_DIR AND EXISTS "${MAHO_GLSLANG_SOURCE_DIR}/CMakeLists.txt")
	set(_RENDER_GLSLANG_SRC "${MAHO_GLSLANG_SOURCE_DIR}")
	message(STATUS "Maho: using MAHO_GLSLANG_SOURCE_DIR=${_RENDER_GLSLANG_SRC}")
else()
	set(_maho_glslang_reuse "${FETCHCONTENT_BASE_DIR}/glslang-src")
	if(EXISTS "${_maho_glslang_reuse}/CMakeLists.txt")
		set(_RENDER_GLSLANG_SRC "${_maho_glslang_reuse}")
		message(STATUS "Maho: reusing glslang at ${_RENDER_GLSLANG_SRC}")
	elseif(MAHO_FETCH_GLSLANG)
		FetchContent_Declare(
			glslang
			GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
			GIT_TAG 14.3.0
			GIT_SHALLOW TRUE
		)
		FetchContent_GetProperties(glslang)
		if(NOT glslang_POPULATED)
			message(STATUS "Maho: Downloading glslang from GitHub (large repo, may take minutes)…")
			FetchContent_Populate(glslang)
		endif()
		if(DEFINED glslang_SOURCE_DIR AND EXISTS "${glslang_SOURCE_DIR}/CMakeLists.txt")
			set(_RENDER_GLSLANG_SRC "${glslang_SOURCE_DIR}")
		endif()
	endif()
	unset(_maho_glslang_reuse)
endif()

if(_RENDER_GLSLANG_SRC AND EXISTS "${_RENDER_GLSLANG_SRC}/CMakeLists.txt")
	set(BUILD_TESTING OFF)
	set(ENABLE_GLSLANG_INSTALL OFF CACHE BOOL "" FORCE)
	set(ENABLE_SPIRV_TOOLS_INSTALL OFF CACHE BOOL "" FORCE)
	set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
	set(ENABLE_OPT OFF CACHE BOOL "" FORCE)

	set(_maho_build_shared_saved "${BUILD_SHARED_LIBS}")
	set(BUILD_SHARED_LIBS OFF)

	set(glslang_SOURCE_DIR "${_RENDER_GLSLANG_SRC}")
	set(glslang_BINARY_DIR "${FETCHCONTENT_BASE_DIR}/glslang-build")
	if(NOT TARGET glslang)
		enable_language(C)
		add_subdirectory(${glslang_SOURCE_DIR} ${glslang_BINARY_DIR} EXCLUDE_FROM_ALL)
	endif()

	set(BUILD_SHARED_LIBS "${_maho_build_shared_saved}")
	unset(_maho_build_shared_saved)

	if(TARGET glslang AND TARGET OSDependent AND TARGET SPIRV)
		set(_RENDER_HAS_GLSLANG 1)
		set_target_properties(glslang PROPERTIES FOLDER "Plugins/Render")
		set_target_properties(OSDependent PROPERTIES FOLDER "Plugins/Render")
		set_target_properties(SPIRV PROPERTIES FOLDER "Plugins/Render")
		if(TARGET GenericCodeGen)
			set_target_properties(GenericCodeGen PROPERTIES FOLDER "Program")
		endif()
		if(TARGET MachineIndependent)
			set_target_properties(MachineIndependent PROPERTIES FOLDER "Program")
		endif()
		message(STATUS "Maho: glslang enabled")
	else()
		message(WARNING "Maho: glslang sources present but required targets missing")
	endif()
else()
	message(WARNING
		"Maho: glslang not found. Shader compilation disabled. "
		"Set MAHO_GLSLANG_SOURCE_DIR or MAHO_FETCH_GLSLANG=ON when network allows.")
endif()

if(_RENDER_HAS_GLSLANG)
	target_link_libraries(${_MOD_TARGET} PRIVATE glslang OSDependent SPIRV)
	target_compile_definitions(${_MOD_TARGET} PRIVATE MAHO_WITH_GLSLANG=1)
	target_include_directories(${_MOD_TARGET} PRIVATE
		"${glslang_SOURCE_DIR}"
		"${glslang_BINARY_DIR}"
	)
	if(TARGET SPIRV-Tools-opt AND TARGET SPIRV-Tools)
		target_link_libraries(${_MOD_TARGET} PRIVATE SPIRV-Tools-opt SPIRV-Tools)
		target_compile_definitions(${_MOD_TARGET} PRIVATE MAHO_WITH_SPIRV_TOOLS=1)
	endif()
endif()

unset(_RENDER_GLSLANG_SRC)
