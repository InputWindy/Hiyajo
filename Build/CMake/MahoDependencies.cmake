# Third-party dependencies for the Maho engine target.

include(FetchContent)

# VS rebuilds re-run CMake when this file changes. Skip git "update" so offline /
# flaky GitHub access does not break an already-populated Intermediate/_deps tree.
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL
	"Skip FetchContent git update when already populated" FORCE)

# If a previous populate left sources but lost CMake stamps, reuse the tree
# instead of calling FetchContent_Populate (which may still hit the network).
# Markers: relative paths that must exist under ${FETCHCONTENT_BASE_DIR}/<name>-src.
macro(maho_fetchcontent_populate_or_reuse _name)
	FetchContent_GetProperties(${_name})
	if(NOT ${_name}_POPULATED)
		string(TOLOWER "${_name}" _maho_fc_lower)
		set(_maho_fc_src "${FETCHCONTENT_BASE_DIR}/${_maho_fc_lower}-src")
		set(_maho_fc_ok FALSE)
		foreach(_maho_fc_marker IN ITEMS ${ARGN})
			if(EXISTS "${_maho_fc_src}/${_maho_fc_marker}")
				set(_maho_fc_ok TRUE)
				break()
			endif()
		endforeach()
		if(_maho_fc_ok)
			set(${_name}_SOURCE_DIR "${_maho_fc_src}")
			set(${_name}_BINARY_DIR "${FETCHCONTENT_BASE_DIR}/${_maho_fc_lower}-build")
			set(${_name}_POPULATED TRUE)
			message(STATUS "Maho: reusing FetchContent ${_name} at ${${_name}_SOURCE_DIR}")
		else()
			FetchContent_Populate(${_name})
		endif()
		unset(_maho_fc_src)
		unset(_maho_fc_ok)
		unset(_maho_fc_lower)
		unset(_maho_fc_marker)
	endif()
endmacro()

set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
	spdlog
	GIT_REPOSITORY https://github.com/gabime/spdlog.git
	GIT_TAG v1.15.3
	GIT_SHALLOW TRUE
)

maho_fetchcontent_populate_or_reuse(spdlog include/spdlog/spdlog.h)
if(spdlog_POPULATED AND NOT TARGET spdlog::spdlog_header_only)
	add_subdirectory(${spdlog_SOURCE_DIR} ${spdlog_BINARY_DIR})
endif()

if(TARGET spdlog)
	set_target_properties(spdlog PROPERTIES FOLDER "ThirdParty")
endif()

# GLFW (window / input / time; CLIENT_API=NO_API for future Vulkan surfaces)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

set(_MAHO_BUILD_SHARED_LIBS_SAVED "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS OFF)

FetchContent_Declare(
	glfw
	GIT_REPOSITORY https://github.com/glfw/glfw.git
	GIT_TAG 3.4
	GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(glfw)

set(BUILD_SHARED_LIBS "${_MAHO_BUILD_SHARED_LIBS_SAVED}")
unset(_MAHO_BUILD_SHARED_LIBS_SAVED)

# GLFW's own CMake uses FOLDER "GLFW3"; keep all GLFW targets under ThirdParty in the .sln.
if(TARGET glfw)
	set_target_properties(glfw PROPERTIES FOLDER "ThirdParty")
endif()
if(TARGET update_mappings)
	set_target_properties(update_mappings PROPERTIES FOLDER "ThirdParty")
endif()

# Vulkan (LunarG SDK via VULKAN_SDK / FindVulkan)
find_package(Vulkan REQUIRED)
message(STATUS "Maho: Vulkan found")
message(STATUS "  Vulkan_INCLUDE_DIRS = ${Vulkan_INCLUDE_DIRS}")
message(STATUS "  Vulkan_LIBRARIES    = ${Vulkan_LIBRARIES}")

find_package(Threads REQUIRED)

get_filename_component(_MAHO_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
get_filename_component(_MAHO_PUBLIC_HEADERS "${_MAHO_REPO_ROOT}/Maho/Source/Public" ABSOLUTE)

# Include as <nlohmann/json.hpp> (vendored single-header under Maho/ThirdParty/nlohmann).
set(_MAHO_VENDORED_NLOHMANN_JSON "${_MAHO_REPO_ROOT}/Maho/ThirdParty/nlohmann")
if(EXISTS "${_MAHO_VENDORED_NLOHMANN_JSON}/json.hpp")
	set(MAHO_NLOHMANN_JSON_INCLUDE_DIR "${_MAHO_REPO_ROOT}/Maho/ThirdParty" CACHE INTERNAL "nlohmann/json include root")
	message(STATUS "Maho: nlohmann/json (vendored) at ${_MAHO_VENDORED_NLOHMANN_JSON}")
else()
	FetchContent_Declare(
		nlohmann_json
		GIT_REPOSITORY https://github.com/nlohmann/json.git
		GIT_TAG v3.11.3
		GIT_SHALLOW TRUE
	)
	FetchContent_MakeAvailable(nlohmann_json)
	# single_include/nlohmann/json.hpp layout from the repo.
	set(MAHO_NLOHMANN_JSON_INCLUDE_DIR "${nlohmann_json_SOURCE_DIR}/single_include" CACHE INTERNAL "nlohmann/json include root")
	message(STATUS "Maho: nlohmann/json (FetchContent) at ${MAHO_NLOHMANN_JSON_INCLUDE_DIR}")
endif()
unset(_MAHO_VENDORED_NLOHMANN_JSON)

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

# STATIC ThirdParty lib (own .vcxproj under ThirdParty/). Avoid OBJECT — VS would
# nest imgui*.obj under Maho's "Object Libraries" filter.
add_library(imgui STATIC ${MAHO_IMGUI_SOURCES} ${MAHO_IMGUI_EXT_SOURCES})
add_library(Maho::ImGui ALIAS imgui)

target_include_directories(imgui
	PUBLIC
		"${MAHO_IMGUI_SOURCE_DIR}"
		"${MAHO_IMGUI_SOURCE_DIR}/backends"
		# IMGUI_USER_CONFIG resolves to Render/UI/ImGuiConfig.h under this include root.
		"${_MAHO_PUBLIC_HEADERS}"
		"${MAHO_IMGUIZMO_SOURCE_DIR}"
		"${MAHO_IMGUI_NODE_EDITOR_SOURCE_DIR}"
		"${MAHO_IMPLOT_SOURCE_DIR}"
		"${MAHO_IMGUI_FILE_DIALOG_SOURCE_DIR}"
		"${MAHO_ICON_FONT_HEADERS_DIR}"
)

target_compile_definitions(imgui
	PUBLIC
		IMGUI_USER_CONFIG="Render/UI/ImGuiConfig.h"
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
		glfw
)

target_compile_features(imgui PUBLIC cxx_std_20)

if(MSVC)
	target_compile_options(imgui PRIVATE /W4 /permissive- /Zc:preprocessor /utf-8)
	set_source_files_properties(${MAHO_IMGUI_EXT_SOURCES} PROPERTIES COMPILE_FLAGS "/W3")
endif()

set_target_properties(imgui PROPERTIES
	FOLDER "ThirdParty"
	POSITION_INDEPENDENT_CODE ON
)

source_group(TREE "${MAHO_IMGUI_SOURCE_DIR}" PREFIX "imgui" FILES ${MAHO_IMGUI_SOURCES})
source_group("imgui_ext" FILES ${MAHO_IMGUI_EXT_SOURCES})
list(LENGTH MAHO_IMGUI_EXT_SOURCES _MAHO_IMGUI_EXT_COUNT)
message(STATUS "Maho: ImGui extensions: ${_MAHO_IMGUI_EXT_COUNT} source file(s)")
unset(_MAHO_IMGUI_EXT_COUNT)

# Engine fonts / editor assets stay in-repo (copied next to the binary at build).
set(_MAHO_TP "${_MAHO_REPO_ROOT}/Maho/ThirdParty")
set(MAHO_ENGINE_FONTS_DIR "${_MAHO_TP}/fonts" CACHE INTERNAL "Engine icon/UI fonts")
set(MAHO_ENGINE_EDITOR_DIR "${_MAHO_TP}/editor" CACHE INTERNAL "Engine editor assets (wallpaper, …)")

# ---------------------------------------------------------------------------
# Lua 5.4 (static) + sol2 (header-only bindings)
# ---------------------------------------------------------------------------
set(_MAHO_VENDORED_LUA "${_MAHO_REPO_ROOT}/Maho/ThirdParty/lua")
if(EXISTS "${_MAHO_VENDORED_LUA}/lua.h")
	set(MAHO_LUA_SOURCE_DIR "${_MAHO_VENDORED_LUA}" CACHE INTERNAL "Lua source directory")
	message(STATUS "Maho: Lua (vendored) at ${MAHO_LUA_SOURCE_DIR}")
elseif(EXISTS "${_MAHO_VENDORED_LUA}/src/lua.h")
	set(MAHO_LUA_SOURCE_DIR "${_MAHO_VENDORED_LUA}/src" CACHE INTERNAL "Lua source directory")
	message(STATUS "Maho: Lua (vendored src/) at ${MAHO_LUA_SOURCE_DIR}")
else()
	FetchContent_Declare(
		lua_src
		GIT_REPOSITORY https://github.com/lua/lua.git
		GIT_TAG v5.4.7
		GIT_SHALLOW TRUE
	)
	maho_fetchcontent_populate_or_reuse(lua_src lua.h)
	# Upstream lua.git keeps sources in the repo root (not src/).
	set(MAHO_LUA_SOURCE_DIR "${lua_src_SOURCE_DIR}" CACHE INTERNAL "Lua source directory")
	message(STATUS "Maho: Lua (FetchContent) at ${MAHO_LUA_SOURCE_DIR}")
endif()

set(MAHO_LUA_SOURCES
	"${MAHO_LUA_SOURCE_DIR}/lapi.c"
	"${MAHO_LUA_SOURCE_DIR}/lauxlib.c"
	"${MAHO_LUA_SOURCE_DIR}/lbaselib.c"
	"${MAHO_LUA_SOURCE_DIR}/lcode.c"
	"${MAHO_LUA_SOURCE_DIR}/lcorolib.c"
	"${MAHO_LUA_SOURCE_DIR}/lctype.c"
	"${MAHO_LUA_SOURCE_DIR}/ldblib.c"
	"${MAHO_LUA_SOURCE_DIR}/ldebug.c"
	"${MAHO_LUA_SOURCE_DIR}/ldo.c"
	"${MAHO_LUA_SOURCE_DIR}/ldump.c"
	"${MAHO_LUA_SOURCE_DIR}/lfunc.c"
	"${MAHO_LUA_SOURCE_DIR}/lgc.c"
	"${MAHO_LUA_SOURCE_DIR}/linit.c"
	"${MAHO_LUA_SOURCE_DIR}/liolib.c"
	"${MAHO_LUA_SOURCE_DIR}/llex.c"
	"${MAHO_LUA_SOURCE_DIR}/lmathlib.c"
	"${MAHO_LUA_SOURCE_DIR}/lmem.c"
	"${MAHO_LUA_SOURCE_DIR}/loadlib.c"
	"${MAHO_LUA_SOURCE_DIR}/lobject.c"
	"${MAHO_LUA_SOURCE_DIR}/lopcodes.c"
	"${MAHO_LUA_SOURCE_DIR}/loslib.c"
	"${MAHO_LUA_SOURCE_DIR}/lparser.c"
	"${MAHO_LUA_SOURCE_DIR}/lstate.c"
	"${MAHO_LUA_SOURCE_DIR}/lstring.c"
	"${MAHO_LUA_SOURCE_DIR}/lstrlib.c"
	"${MAHO_LUA_SOURCE_DIR}/ltable.c"
	"${MAHO_LUA_SOURCE_DIR}/ltablib.c"
	"${MAHO_LUA_SOURCE_DIR}/ltm.c"
	"${MAHO_LUA_SOURCE_DIR}/lundump.c"
	"${MAHO_LUA_SOURCE_DIR}/lutf8lib.c"
	"${MAHO_LUA_SOURCE_DIR}/lvm.c"
	"${MAHO_LUA_SOURCE_DIR}/lzio.c"
)

add_library(lua STATIC ${MAHO_LUA_SOURCES})
add_library(Maho::Lua ALIAS lua)
target_include_directories(lua PUBLIC "${MAHO_LUA_SOURCE_DIR}")
set_target_properties(lua PROPERTIES
	FOLDER "ThirdParty"
	POSITION_INDEPENDENT_CODE ON
	C_STANDARD 99
)
if(MSVC)
	target_compile_definitions(lua PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

set(_MAHO_VENDORED_SOL2 "${_MAHO_REPO_ROOT}/Maho/ThirdParty/sol2")
if(EXISTS "${_MAHO_VENDORED_SOL2}/include/sol/sol.hpp")
	set(MAHO_SOL2_INCLUDE_DIR "${_MAHO_VENDORED_SOL2}/include" CACHE INTERNAL "sol2 include directory")
	message(STATUS "Maho: sol2 (vendored) at ${MAHO_SOL2_INCLUDE_DIR}")
else()
	FetchContent_Declare(
		sol2
		GIT_REPOSITORY https://github.com/ThePhD/sol2.git
		GIT_TAG v3.3.1
		GIT_SHALLOW TRUE
	)
	maho_fetchcontent_populate_or_reuse(sol2 include/sol/sol.hpp)
	set(MAHO_SOL2_INCLUDE_DIR "${sol2_SOURCE_DIR}/include" CACHE INTERNAL "sol2 include directory")
	message(STATUS "Maho: sol2 (FetchContent) at ${MAHO_SOL2_INCLUDE_DIR}")
endif()

unset(_MAHO_VENDORED_LUA)
unset(_MAHO_VENDORED_SOL2)

# ---------------------------------------------------------------------------
# refl-cpp (header-only) — optional / legacy; FObject reflection uses ObjectReflect.h + codegen.
# ---------------------------------------------------------------------------
set(_MAHO_VENDORED_REFL "${_MAHO_REPO_ROOT}/Maho/ThirdParty/refl-cpp")
if(EXISTS "${_MAHO_VENDORED_REFL}/include/refl.hpp")
	set(MAHO_REFL_INCLUDE_DIR "${_MAHO_VENDORED_REFL}/include" CACHE INTERNAL "refl-cpp include directory")
	message(STATUS "Maho: refl-cpp (vendored) at ${MAHO_REFL_INCLUDE_DIR}")
elseif(EXISTS "${_MAHO_VENDORED_REFL}/refl.hpp")
	set(MAHO_REFL_INCLUDE_DIR "${_MAHO_VENDORED_REFL}" CACHE INTERNAL "refl-cpp include directory")
	message(STATUS "Maho: refl-cpp (vendored flat) at ${MAHO_REFL_INCLUDE_DIR}")
else()
	FetchContent_Declare(
		refl_cpp
		GIT_REPOSITORY https://github.com/veselink1/refl-cpp.git
		GIT_TAG v0.12.4
		GIT_SHALLOW TRUE
	)
	maho_fetchcontent_populate_or_reuse(refl_cpp include/refl.hpp)
	set(MAHO_REFL_INCLUDE_DIR "${refl_cpp_SOURCE_DIR}/include" CACHE INTERNAL "refl-cpp include directory")
	message(STATUS "Maho: refl-cpp (FetchContent) at ${MAHO_REFL_INCLUDE_DIR}")
endif()
unset(_MAHO_VENDORED_REFL)

# -----------------------------------------------------------------------------
# GLM (OpenGL Mathematics) — header-only math library for ECS transforms / render math.
# Prefer vendored tree; FetchContent only if missing (needs network).
# -----------------------------------------------------------------------------
set(_MAHO_VENDORED_GLM "${_MAHO_REPO_ROOT}/Maho/ThirdParty/glm")
if(EXISTS "${_MAHO_VENDORED_GLM}/glm/glm.hpp")
	set(MAHO_GLM_INCLUDE_DIR "${_MAHO_VENDORED_GLM}" CACHE INTERNAL "glm include directory")
	message(STATUS "Maho: glm (vendored) at ${MAHO_GLM_INCLUDE_DIR}")
else()
	FetchContent_Declare(
		glm
		GIT_REPOSITORY https://github.com/g-truc/glm.git
		GIT_TAG 1.0.1
		GIT_SHALLOW TRUE
	)
	maho_fetchcontent_populate_or_reuse(glm glm/glm.hpp)
	set(MAHO_GLM_INCLUDE_DIR "${glm_SOURCE_DIR}" CACHE INTERNAL "glm include directory")
	message(STATUS "Maho: glm (FetchContent) at ${MAHO_GLM_INCLUDE_DIR}")
endif()
unset(_MAHO_VENDORED_GLM)

# -----------------------------------------------------------------------------
# Vulkan Memory Allocator (GPUOpen) — header-only; VMA_IMPLEMENTATION in one Maho .cpp
# Prefer vendored tree; FetchContent only if missing (needs network).
# -----------------------------------------------------------------------------
set(_MAHO_VENDORED_VMA "${_MAHO_REPO_ROOT}/Maho/ThirdParty/VulkanMemoryAllocator")
if(EXISTS "${_MAHO_VENDORED_VMA}/include/vk_mem_alloc.h")
	set(MAHO_VMA_INCLUDE_DIR "${_MAHO_VENDORED_VMA}/include" CACHE INTERNAL "VulkanMemoryAllocator include directory" FORCE)
	message(STATUS "Maho: VMA (vendored) at ${MAHO_VMA_INCLUDE_DIR}")
else()
	FetchContent_Declare(
		vma
		GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
		GIT_TAG v3.2.1
		GIT_SHALLOW TRUE
	)
	maho_fetchcontent_populate_or_reuse(vma include/vk_mem_alloc.h)
	set(MAHO_VMA_INCLUDE_DIR "${vma_SOURCE_DIR}/include" CACHE INTERNAL "VulkanMemoryAllocator include directory" FORCE)
	message(STATUS "Maho: VMA (FetchContent) at ${MAHO_VMA_INCLUDE_DIR}")
endif()
unset(_MAHO_VENDORED_VMA)

# -----------------------------------------------------------------------------
# KTX-Software (libktx) — KTX2 Import/Export for UTexture* (Game CPU path)
# OpenImageIO remains the preferred raster codec upgrade; Win32 uses WIC until then.
#
# Enable when sources are available:
#   -DMAHO_KTX_SOURCE_DIR=<checkout>   OR pre-populate Intermediate/_deps/ktx_software-src
#   -DMAHO_FETCH_LIBKTX=ON             to FetchContent from GitHub (needs network)
# Without sources, configure succeeds; KTX2 path is compile-disabled (WIC still works).
# -----------------------------------------------------------------------------
option(MAHO_WITH_LIBKTX "Build with libktx when KTX-Software sources are available" ON)
option(MAHO_FETCH_LIBKTX "FetchContent KTX-Software from GitHub (slow / needs network)" OFF)
set(MAHO_HAS_LIBKTX 0)
set(MAHO_KTX_SOURCE_DIR "" CACHE PATH "Pre-cloned KTX-Software root (optional)")
if(MAHO_WITH_LIBKTX)
	set(KTX_FEATURE_TOOLS OFF CACHE BOOL "" FORCE)
	set(KTX_FEATURE_DOC OFF CACHE BOOL "" FORCE)
	set(KTX_FEATURE_TESTS OFF CACHE BOOL "" FORCE)
	set(KTX_FEATURE_GL_UPLOAD OFF CACHE BOOL "" FORCE)
	set(KTX_FEATURE_VK_UPLOAD OFF CACHE BOOL "" FORCE)
	set(KTX_FEATURE_STATIC_LIBRARY ON CACHE BOOL "" FORCE)

	set(_MAHO_KTX_SRC "")
	if(MAHO_KTX_SOURCE_DIR AND EXISTS "${MAHO_KTX_SOURCE_DIR}/CMakeLists.txt")
		set(_MAHO_KTX_SRC "${MAHO_KTX_SOURCE_DIR}")
		message(STATUS "Maho: using MAHO_KTX_SOURCE_DIR=${_MAHO_KTX_SRC}")
	else()
		set(_maho_ktx_reuse "${FETCHCONTENT_BASE_DIR}/ktx_software-src")
		if(EXISTS "${_maho_ktx_reuse}/CMakeLists.txt")
			set(_MAHO_KTX_SRC "${_maho_ktx_reuse}")
			message(STATUS "Maho: reusing KTX-Software at ${_MAHO_KTX_SRC}")
		elseif(MAHO_FETCH_LIBKTX)
			FetchContent_Declare(
				ktx_software
				GIT_REPOSITORY https://github.com/KhronosGroup/KTX-Software.git
				GIT_TAG v4.3.2
				GIT_SHALLOW TRUE
			)
			FetchContent_GetProperties(ktx_software)
			if(NOT ktx_software_POPULATED)
				FetchContent_Populate(ktx_software)
			endif()
			if(DEFINED ktx_software_SOURCE_DIR AND EXISTS "${ktx_software_SOURCE_DIR}/CMakeLists.txt")
				set(_MAHO_KTX_SRC "${ktx_software_SOURCE_DIR}")
			endif()
		endif()
		unset(_maho_ktx_reuse)
	endif()

	if(_MAHO_KTX_SRC AND EXISTS "${_MAHO_KTX_SRC}/CMakeLists.txt")
		# KTX-Software FindBash requires a Unix-ish bash (Git for Windows).
		if(WIN32 AND NOT BASH_EXECUTABLE)
			foreach(_maho_bash_candidate
				"$ENV{ProgramFiles}/Git/bin/bash.exe"
				"$ENV{ProgramFiles\(x86\)}/Git/bin/bash.exe"
				"D:/Git/bin/bash.exe"
				"C:/Program Files/Git/bin/bash.exe")
				if(EXISTS "${_maho_bash_candidate}")
					set(BASH_EXECUTABLE "${_maho_bash_candidate}" CACHE FILEPATH "bash for KTX-Software" FORCE)
					break()
				endif()
			endforeach()
			# Same install as `where git` → …/cmd/git.exe → sibling bin/bash.exe
			if(NOT BASH_EXECUTABLE)
				find_program(_maho_git_exe NAMES git git.exe)
				if(_maho_git_exe)
					get_filename_component(_maho_git_cmd "${_maho_git_exe}" DIRECTORY)
					get_filename_component(_maho_git_root "${_maho_git_cmd}" DIRECTORY)
					if(EXISTS "${_maho_git_root}/bin/bash.exe")
						set(BASH_EXECUTABLE "${_maho_git_root}/bin/bash.exe" CACHE FILEPATH "bash for KTX-Software" FORCE)
					endif()
					unset(_maho_git_cmd)
					unset(_maho_git_root)
				endif()
				unset(_maho_git_exe)
			endif()
		endif()
		set(ktx_software_SOURCE_DIR "${_MAHO_KTX_SRC}")
		set(ktx_software_BINARY_DIR "${FETCHCONTENT_BASE_DIR}/ktx_software-build")
		# Parent game projects often declare LANGUAGES CXX only; libktx needs C.
		enable_language(C)
		if(NOT TARGET ktx)
			add_subdirectory(${ktx_software_SOURCE_DIR} ${ktx_software_BINARY_DIR} EXCLUDE_FROM_ALL)
		endif()
		if(TARGET ktx)
			set(MAHO_HAS_LIBKTX 1)
			set_target_properties(ktx PROPERTIES FOLDER "ThirdParty")
			message(STATUS "Maho: KTX-Software (libktx) enabled")
		else()
			message(WARNING "Maho: KTX-Software sources present but target 'ktx' missing")
		endif()
	else()
		message(STATUS
			"Maho: libktx skipped (no KTX-Software sources). "
			"Set MAHO_KTX_SOURCE_DIR or MAHO_FETCH_LIBKTX=ON when network allows.")
	endif()
	unset(_MAHO_KTX_SRC)
endif()

# Optional OpenImageIO (heavy). Default OFF — Win32 raster uses WIC; enable when deps ready.
option(MAHO_WITH_OPENIMAGEIO "FetchContent OpenImageIO for raster texture IO" OFF)
set(MAHO_HAS_OPENIMAGEIO 0)
if(MAHO_WITH_OPENIMAGEIO)
	message(STATUS "Maho: OpenImageIO requested — wire FetchContent in a follow-up if needed")
endif()

# -----------------------------------------------------------------------------
# Assimp — model scene decode for UPrefab import (CPU only; Private MeshModelCodec)
#
# Default: FetchContent from GitHub when no local sources (needs network on first configure).
# Override:
#   -DMAHO_ASSIMP_SOURCE_DIR=<checkout>  use a local tree (skips fetch)
#   -DMAHO_FETCH_ASSIMP=OFF              skip fetch; decode disabled if no sources
#   -DMAHO_WITH_ASSIMP=OFF               never link Assimp
# -----------------------------------------------------------------------------
option(MAHO_WITH_ASSIMP "Build with Assimp when sources are available" ON)
option(MAHO_FETCH_ASSIMP "FetchContent Assimp from GitHub when local sources missing" ON)
set(MAHO_HAS_ASSIMP 0)
set(MAHO_ASSIMP_SOURCE_DIR "" CACHE PATH "Pre-cloned Assimp root (optional)")
if(MAHO_WITH_ASSIMP)
	set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
	set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
	set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
	set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
	set(ASSIMP_INJECT_DEBUG_POSTFIX OFF CACHE BOOL "" FORCE)
	set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
	set(ASSIMP_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
	# MSVC C4819 (codepage) in contrib/clipper otherwise fails the Assimp default /WX.
	set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)

	set(_MAHO_ASSIMP_SRC "")
	if(MAHO_ASSIMP_SOURCE_DIR AND EXISTS "${MAHO_ASSIMP_SOURCE_DIR}/CMakeLists.txt")
		set(_MAHO_ASSIMP_SRC "${MAHO_ASSIMP_SOURCE_DIR}")
		message(STATUS "Maho: using MAHO_ASSIMP_SOURCE_DIR=${_MAHO_ASSIMP_SRC}")
	else()
		set(_maho_assimp_reuse "${FETCHCONTENT_BASE_DIR}/assimp-src")
		if(EXISTS "${_maho_assimp_reuse}/CMakeLists.txt")
			set(_MAHO_ASSIMP_SRC "${_maho_assimp_reuse}")
			message(STATUS "Maho: reusing Assimp at ${_MAHO_ASSIMP_SRC}")
		elseif(MAHO_FETCH_ASSIMP)
			# v5.4+ needs CMake >= 3.22; pin 5.3.1 for Maho cmake_minimum 3.20 / common 3.21 hosts.
			FetchContent_Declare(
				assimp
				GIT_REPOSITORY https://github.com/assimp/assimp.git
				GIT_TAG v5.3.1
				GIT_SHALLOW TRUE
			)
			FetchContent_GetProperties(assimp)
			if(NOT assimp_POPULATED)
				FetchContent_Populate(assimp)
			endif()
			if(DEFINED assimp_SOURCE_DIR AND EXISTS "${assimp_SOURCE_DIR}/CMakeLists.txt")
				set(_MAHO_ASSIMP_SRC "${assimp_SOURCE_DIR}")
			endif()
		endif()
		unset(_maho_assimp_reuse)
	endif()

	if(_MAHO_ASSIMP_SRC AND EXISTS "${_MAHO_ASSIMP_SRC}/CMakeLists.txt")
		set(assimp_SOURCE_DIR "${_MAHO_ASSIMP_SRC}")
		set(assimp_BINARY_DIR "${FETCHCONTENT_BASE_DIR}/assimp-build")
		enable_language(C)
		if(NOT TARGET assimp)
			add_subdirectory(${assimp_SOURCE_DIR} ${assimp_BINARY_DIR} EXCLUDE_FROM_ALL)
		endif()
		if(TARGET assimp)
			set(MAHO_HAS_ASSIMP 1)
			set_target_properties(assimp PROPERTIES FOLDER "ThirdParty")
			if(TARGET zlibstatic)
				set_target_properties(zlibstatic PROPERTIES FOLDER "ThirdParty")
			endif()
			message(STATUS "Maho: Assimp enabled")
		else()
			message(WARNING "Maho: Assimp sources present but target 'assimp' missing")
		endif()
	else()
		message(STATUS
			"Maho: Assimp skipped (no sources). "
			"Set MAHO_ASSIMP_SOURCE_DIR or MAHO_FETCH_ASSIMP=ON when network allows.")
	endif()
	unset(_MAHO_ASSIMP_SRC)
endif()

# -----------------------------------------------------------------------------
# glslang — Khronos GLSL → SPIR-V compiler (runtime shader compilation)
#
# Default: FetchContent from GitHub (required; shader system won't work without it).
# Override:
#   -DMAHO_GLSLANG_SOURCE_DIR=<checkout>  use a local tree (skips fetch)
# -----------------------------------------------------------------------------
option(MAHO_FETCH_GLSLANG "FetchContent glslang from GitHub" ON)
set(MAHO_HAS_GLSLANG 0)
set(MAHO_GLSLANG_SOURCE_DIR "" CACHE PATH "Pre-cloned glslang root (optional)")
set(MAHO_GLSLANG_BINARY_DIR "" CACHE PATH "glslang build dir (optional, paired with SOURCE_DIR)")

set(_MAHO_GLSLANG_SRC "")
if(MAHO_GLSLANG_SOURCE_DIR AND EXISTS "${MAHO_GLSLANG_SOURCE_DIR}/CMakeLists.txt")
	set(_MAHO_GLSLANG_SRC "${MAHO_GLSLANG_SOURCE_DIR}")
	message(STATUS "Maho: using MAHO_GLSLANG_SOURCE_DIR=${_MAHO_GLSLANG_SRC}")
else()
	set(_maho_glslang_reuse "${FETCHCONTENT_BASE_DIR}/glslang-src")
	if(EXISTS "${_maho_glslang_reuse}/CMakeLists.txt")
		set(_MAHO_GLSLANG_SRC "${_maho_glslang_reuse}")
		message(STATUS "Maho: reusing glslang at ${_MAHO_GLSLANG_SRC}")
	elseif(MAHO_FETCH_GLSLANG)
		FetchContent_Declare(
			glslang
			GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
			GIT_TAG 14.3.0
			GIT_SHALLOW TRUE
		)
		FetchContent_GetProperties(glslang)
		if(NOT glslang_POPULATED)
			FetchContent_Populate(glslang)
		endif()
		if(DEFINED glslang_SOURCE_DIR AND EXISTS "${glslang_SOURCE_DIR}/CMakeLists.txt")
			set(_MAHO_GLSLANG_SRC "${glslang_SOURCE_DIR}")
		endif()
	endif()
	unset(_maho_glslang_reuse)
endif()

if(_MAHO_GLSLANG_SRC AND EXISTS "${_MAHO_GLSLANG_SRC}/CMakeLists.txt")
	set(BUILD_TESTING OFF)
	set(ENABLE_GLSLANG_INSTALL OFF CACHE BOOL "" FORCE)
	set(ENABLE_SPIRV_TOOLS_INSTALL OFF CACHE BOOL "" FORCE)
	set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
	set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
	set(BUILD_SHARED_LIBS OFF)

	set(glslang_SOURCE_DIR "${_MAHO_GLSLANG_SRC}")
	set(glslang_BINARY_DIR "${FETCHCONTENT_BASE_DIR}/glslang-build")
	if(NOT TARGET glslang)
		enable_language(C)
		add_subdirectory(${glslang_SOURCE_DIR} ${glslang_BINARY_DIR} EXCLUDE_FROM_ALL)
	endif()
	if(TARGET glslang AND TARGET OSDependent AND TARGET SPIRV)
		set(MAHO_HAS_GLSLANG 1)
		set_target_properties(glslang PROPERTIES FOLDER "ThirdParty")
		set_target_properties(OSDependent PROPERTIES FOLDER "ThirdParty")
		set_target_properties(SPIRV PROPERTIES FOLDER "ThirdParty")
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
unset(_MAHO_GLSLANG_SRC)

# SPIRV-Tools (optimizer / linker — optional, for future shader optimization)
set(MAHO_HAS_SPIRV_TOOLS 0)
if(_MAHO_GLSLANG_SRC AND EXISTS "${_MAHO_GLSLANG_SRC}/External/spirv-tools/CMakeLists.txt")
	if(TARGET SPIRV-Tools-opt)
		set(MAHO_HAS_SPIRV_TOOLS 1)
		set_target_properties(SPIRV-Tools-opt PROPERTIES FOLDER "ThirdParty")
		set_target_properties(SPIRV-Tools PROPERTIES FOLDER "ThirdParty")
	endif()
endif()

unset(_MAHO_REPO_ROOT)
unset(_MAHO_PUBLIC_HEADERS)
unset(_MAHO_TP)
