cmake_minimum_required(VERSION 3.20)

set(MAHO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(MAHO_SOURCE_DIR "${MAHO_ROOT}/Source")
set(MAHO_PUBLIC_DIR "${MAHO_SOURCE_DIR}/Public")
set(MAHO_PRIVATE_DIR "${MAHO_SOURCE_DIR}/Private")
set(MAHO_SHADERS_DIR "${MAHO_ROOT}/Shaders")
set(MAHO_PLUGINS_DIR "${MAHO_ROOT}/Plugins")

# Resolve Build/CMake helpers when Maho is added from engine workspace or external .cproject.
if(NOT DEFINED MAHO_ENGINE_BUILD_CMAKE_DIR)
	get_filename_component(MAHO_ENGINE_BUILD_CMAKE_DIR "${MAHO_ROOT}/../Build/CMake" ABSOLUTE)
endif()
list(APPEND CMAKE_MODULE_PATH "${MAHO_ENGINE_BUILD_CMAKE_DIR}")
include(MahoDependencies)
include(MahoHelpers)

set(MAHO_GENERATED_DIR "${MAHO_ROOT}/Intermediate/Generated")

# Engine repo root (Tools/python + object_reflect_codegen.py).
# Installer layout: Tools/python/python.exe ; venv: Tools/python/Scripts/python.exe
get_filename_component(_MAHO_ENGINE_ROOT_FOR_CODEGEN "${MAHO_ROOT}/.." ABSOLUTE)
set(_MAHO_OBJECT_REFLECT_CODEGEN_PY "${_MAHO_ENGINE_ROOT_FOR_CODEGEN}/Tools/object_reflect_codegen.py")
set(_MAHO_LOCAL_PYTHON "${_MAHO_ENGINE_ROOT_FOR_CODEGEN}/Tools/python/python.exe")
if(NOT EXISTS "${_MAHO_LOCAL_PYTHON}")
	set(_MAHO_LOCAL_PYTHON "${_MAHO_ENGINE_ROOT_FOR_CODEGEN}/Tools/python/Scripts/python.exe")
endif()
set(_MAHO_GEN_OBJECT_H "${MAHO_GENERATED_DIR}/ObjectReflectTypes.gen.h")
set(_MAHO_GEN_OBJECT_CPP "${MAHO_GENERATED_DIR}/ObjectReflectTypes.gen.cpp")

# Build-time FObject reflection codegen.
if(NOT EXISTS "${_MAHO_LOCAL_PYTHON}")
	message(FATAL_ERROR
		"Maho: engine-local Python missing under Tools/python (or Scripts).\n"
		"  Tried:\n"
		"    ${_MAHO_ENGINE_ROOT_FOR_CODEGEN}/Tools/python/python.exe\n"
		"    ${_MAHO_ENGINE_ROOT_FOR_CODEGEN}/Tools/python/Scripts/python.exe\n"
		"Run setup.bat in the Maho engine root, then re-configure.")
endif()
if(NOT EXISTS "${_MAHO_OBJECT_REFLECT_CODEGEN_PY}")
	message(FATAL_ERROR "Maho: missing ${_MAHO_OBJECT_REFLECT_CODEGEN_PY}")
endif()

execute_process(
	COMMAND "${_MAHO_LOCAL_PYTHON}" "${_MAHO_OBJECT_REFLECT_CODEGEN_PY}"
	WORKING_DIRECTORY "${_MAHO_ENGINE_ROOT_FOR_CODEGEN}"
	RESULT_VARIABLE _MAHO_CODEGEN_RC
	OUTPUT_VARIABLE _MAHO_CODEGEN_OUT
	ERROR_VARIABLE _MAHO_CODEGEN_ERR
)
if(NOT _MAHO_CODEGEN_RC EQUAL 0)
	message(FATAL_ERROR
		"Maho object reflect codegen failed (exit ${_MAHO_CODEGEN_RC}):\n"
		"${_MAHO_CODEGEN_OUT}${_MAHO_CODEGEN_ERR}")
endif()
message(STATUS "Maho: object reflect codegen OK")

maho_collect_sources(MAHO_SOURCES "${MAHO_SOURCE_DIR}")

# Drop anything under Intermediate/Generated from the glob — added explicitly below.
set(_MAHO_SOURCES_FILTERED "")
foreach(_src IN LISTS MAHO_SOURCES)
	string(FIND "${_src}" "/Intermediate/Generated/" _maho_gen_pos)
	string(FIND "${_src}" "\\Intermediate\\Generated\\" _maho_gen_pos_win)
	if(_maho_gen_pos EQUAL -1 AND _maho_gen_pos_win EQUAL -1)
		list(APPEND _MAHO_SOURCES_FILTERED "${_src}")
endif()
endforeach()
set(MAHO_SOURCES "${_MAHO_SOURCES_FILTERED}")

# Generated sources stay out of MAHO_SOURCES so they don't appear in VS filters.
# They are added to the target via target_sources later.
set(MAHO_GEN_SOURCES



)
list(REMOVE_DUPLICATES MAHO_SOURCES)

if(MAHO_SOURCES STREQUAL "")
	message(FATAL_ERROR "Maho: no sources found under ${MAHO_SOURCE_DIR}")
endif()

if(NOT TARGET imgui)
	message(FATAL_ERROR "Maho: imgui ThirdParty target was not created (MahoDependencies.cmake)")
endif()


if(MAHO_BUILD_SHARED)
	add_library(Maho SHARED ${MAHO_SOURCES})
	target_compile_definitions(Maho
		PUBLIC  MAHO_BUILD_SHARED=1
		PRIVATE MAHO_EXPORTS=1
	)
else()
	add_library(Maho STATIC ${MAHO_SOURCES})
endif()

add_library(Maho::Engine ALIAS Maho)

# Regenerate FObject reflection tables on every Maho build.
add_custom_command(TARGET Maho PRE_BUILD
	COMMAND "${_MAHO_LOCAL_PYTHON}" "${_MAHO_OBJECT_REFLECT_CODEGEN_PY}"
	WORKING_DIRECTORY "${_MAHO_ENGINE_ROOT_FOR_CODEGEN}"
	COMMENT "Maho: regenerate ObjectReflectTypes"
	VERBATIM
)

if(NOT MAHO_VMA_INCLUDE_DIR)
	message(FATAL_ERROR "Maho: VMA include dir was not set (MahoDependencies.cmake)")
endif()

target_include_directories(Maho
	PUBLIC
		"${MAHO_PUBLIC_DIR}"
		"${MAHO_GENERATED_DIR}"
		"${MAHO_GLM_INCLUDE_DIR}"
	PRIVATE
		"${MAHO_PRIVATE_DIR}"
		"${MAHO_NLOHMANN_JSON_INCLUDE_DIR}"
		"${MAHO_VMA_INCLUDE_DIR}"
		"${MAHO_SOL2_INCLUDE_DIR}"
)

# PUBLIC: game code uses MAHO_* macros that expand to spdlog calls.
# Header-only avoids duplicating a static spdlog into both Maho.dll and the game EXE.
# ImGui: separate ThirdParty static lib; Maho.dll consumes headers + links all TUs (WHOLEARCHIVE).
target_link_libraries(Maho
	PUBLIC
		spdlog::spdlog_header_only
		Vulkan::Vulkan
	PRIVATE
		glfw
		Threads::Threads
		lua
)

if(MAHO_HAS_LIBKTX)
	target_link_libraries(Maho PRIVATE ktx)
	target_compile_definitions(Maho PRIVATE MAHO_WITH_LIBKTX=1)
endif()
if(MAHO_HAS_OPENIMAGEIO)
	target_compile_definitions(Maho PRIVATE MAHO_WITH_OPENIMAGEIO=1)
endif()

# Assimp — model decode for the engine resource codec (MeshModelCodec).
if(TARGET assimp)
	target_link_libraries(Maho PRIVATE assimp)
	target_compile_definitions(Maho PRIVATE MAHO_WITH_ASSIMP=1)
	target_include_directories(Maho PRIVATE
		"${assimp_SOURCE_DIR}/include"
		"${assimp_BINARY_DIR}/include"
	)
endif()
if(MAHO_HAS_GLSLANG)
	target_link_libraries(Maho PRIVATE glslang OSDependent SPIRV)
	target_compile_definitions(Maho PRIVATE MAHO_WITH_GLSLANG=1)
	target_include_directories(Maho PRIVATE
		"${glslang_SOURCE_DIR}"
		"${glslang_BINARY_DIR}"
	)
	# SPIRV-Tools (optimizer) — optional.
	if(MAHO_HAS_SPIRV_TOOLS)
		target_link_libraries(Maho PRIVATE SPIRV-Tools-opt SPIRV-Tools)
		target_compile_definitions(Maho PRIVATE MAHO_WITH_SPIRV_TOOLS=1)
endif()
endif()

if(TARGET zlibstatic)
	target_link_libraries(Maho PRIVATE zlibstatic)
	target_compile_definitions(Maho PRIVATE MAHO_WITH_ZLIB=1)
	target_include_directories(Maho PRIVATE
		$<TARGET_PROPERTY:zlibstatic,SOURCE_DIR>
		$<TARGET_PROPERTY:zlibstatic,BINARY_DIR>
	)
endif()

# Re-export ImGui include paths / defs so game TUs can #include <imgui.h> against Maho.dll.
target_include_directories(Maho
	PUBLIC
		$<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>
)
target_compile_definitions(Maho
	PUBLIC
		$<TARGET_PROPERTY:imgui,INTERFACE_COMPILE_DEFINITIONS>
)

# Pull every imgui TU into Maho.dll (demo / widgets otherwise dropped by the linker).
# Do not also target_link_libraries(imgui) on MSVC shared — that + WHOLEARCHIVE duplicates.
add_dependencies(Maho imgui)
if(MAHO_BUILD_SHARED)
	if(MSVC)
		target_link_options(Maho PRIVATE "/WHOLEARCHIVE:$<TARGET_FILE:imgui>")
	else()
		target_link_libraries(Maho PRIVATE
			"-Wl,--whole-archive"
			imgui
			"-Wl,--no-whole-archive"
		)
endif()
else()
	target_link_libraries(Maho PRIVATE imgui)
endif()

target_compile_definitions(Maho
	PUBLIC
		MAHO_WITH_VULKAN=1
		MAHO_WITH_LUA=1
		MAHO_WITH_REFLECT=1
		MAHO_PLATFORM_${MAHO_PLATFORM_NAME}=1
)

if(WIN32)
	target_compile_definitions(Maho PRIVATE VK_USE_PLATFORM_WIN32_KHR=1)
	target_link_libraries(Maho PRIVATE winhttp)
endif()

# Absolute engine root for locating Tools/AgentBridge at runtime (dev builds).
file(TO_CMAKE_PATH "${_MAHO_ENGINE_ROOT_FOR_CODEGEN}" _MAHO_ENGINE_ROOT_PATH)
string(REPLACE "\\" "/" _MAHO_ENGINE_ROOT_FWD "${_MAHO_ENGINE_ROOT_PATH}")
target_compile_definitions(Maho PRIVATE "MAHO_ENGINE_ROOT=\"${_MAHO_ENGINE_ROOT_FWD}\"")

target_compile_features(Maho PUBLIC cxx_std_20)

if(MSVC)
	target_compile_options(Maho PRIVATE /W4 /permissive- /Zc:preprocessor /utf-8)
	set_target_properties(Maho PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS OFF)
else()
	target_compile_options(Maho PRIVATE -Wall -Wextra -Wpedantic)
	if(MAHO_BUILD_SHARED)
		set_target_properties(Maho PROPERTIES
			CXX_VISIBILITY_PRESET hidden
			VISIBILITY_INLINES_HIDDEN ON
		)
endif()
endif()

maho_set_output_dirs(Maho)
maho_source_group_mirrors("${MAHO_SOURCE_DIR}" ${MAHO_SOURCES})
# Generated codegen files: compiled but not shown in VS source tree.
target_sources(Maho PRIVATE ${MAHO_GEN_SOURCES})
set_target_properties(Maho PROPERTIES
	FOLDER "Engine"
	OUTPUT_NAME "Maho"
)

maho_copy_shaders(Maho "${MAHO_SHADERS_DIR}" "Engine/Shaders")
if(DEFINED MAHO_ENGINE_FONTS_DIR AND IS_DIRECTORY "${MAHO_ENGINE_FONTS_DIR}")
	add_custom_command(TARGET Maho POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Maho>/Engine/Fonts"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${MAHO_ENGINE_FONTS_DIR}"
			"$<TARGET_FILE_DIR:Maho>/Engine/Fonts"
		COMMENT "Copy engine Fonts"
		VERBATIM
	)
	maho_install_directory("${MAHO_ENGINE_FONTS_DIR}" "Engine/Fonts")
endif()
if(DEFINED MAHO_ENGINE_EDITOR_DIR AND IS_DIRECTORY "${MAHO_ENGINE_EDITOR_DIR}")
	add_custom_command(TARGET Maho POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Maho>/Engine/Editor"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${MAHO_ENGINE_EDITOR_DIR}"
			"$<TARGET_FILE_DIR:Maho>/Engine/Editor"
		COMMENT "Copy engine Editor assets"
		VERBATIM
	)
	maho_install_directory("${MAHO_ENGINE_EDITOR_DIR}" "Engine/Editor")
endif()

# Preserve -DMAHO_ENGINE_ROOT from game projects (repo root). Never overwrite with Maho/.
if(NOT MAHO_ENGINE_ROOT)
	get_filename_component(MAHO_ENGINE_ROOT "${MAHO_ROOT}/.." ABSOLUTE)
endif()
set(MAHO_ENGINE_SHADERS_DIR "${MAHO_SHADERS_DIR}" CACHE INTERNAL "Maho engine shaders")
set(MAHO_ENGINE_PLUGINS_DIR "${MAHO_PLUGINS_DIR}" CACHE INTERNAL "Maho engine plugins")

maho_install_runtime_target(Maho)
maho_install_directory("${MAHO_SHADERS_DIR}" "Engine/Shaders")
maho_install_directory("${MAHO_PLUGINS_DIR}" "Engine/Plugins")

set(_MAHO_SCAN_PLUGINS_PY "${_MAHO_ENGINE_ROOT_FOR_CODEGEN}/Tools/scan_plugins.py")
if(MAHO_CPROJECT)
	maho_add_plugin_modules(
		CPROJECT "${MAHO_CPROJECT}"
		ENGINE_TARGET Maho
		SCAN_SCRIPT "${_MAHO_SCAN_PLUGINS_PY}"
		PYTHON_EXE "${_MAHO_LOCAL_PYTHON}"
	)
else()
	maho_add_plugin_modules(
		PLUGINS_DIR "${MAHO_PLUGINS_DIR}"
		ENGINE_TARGET Maho
		SCAN_SCRIPT "${_MAHO_SCAN_PLUGINS_PY}"
		PYTHON_EXE "${_MAHO_LOCAL_PYTHON}"
	)
endif()
