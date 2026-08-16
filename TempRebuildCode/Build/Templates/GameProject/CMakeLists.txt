cmake_minimum_required(VERSION 3.20)

# Standalone Maho game project (generated from Build/Templates/GameProject).
# generateProject.py passes -DMAHO_ENGINE_ROOT=<engine repo>

project({{PROJECT_NAME}}
	VERSION 0.1.0
	DESCRIPTION "{{DESCRIPTION}}"
	LANGUAGES CXX
)

if(NOT MAHO_ENGINE_ROOT)
	message(FATAL_ERROR "MAHO_ENGINE_ROOT is not set. Open/generate via the .cproject file.")
endif()

file(TO_CMAKE_PATH "${MAHO_ENGINE_ROOT}" MAHO_ENGINE_ROOT)
if(NOT EXISTS "${MAHO_ENGINE_ROOT}/Maho/CMakeLists.txt")
	message(FATAL_ERROR "Invalid MAHO_ENGINE_ROOT: ${MAHO_ENGINE_ROOT}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT {{PROJECT_NAME}})

list(APPEND CMAKE_MODULE_PATH "${MAHO_ENGINE_ROOT}/Build/CMake")
include(MahoDirectories)
include(MahoHelpers)

maho_ensure_workspace_dirs()
maho_warn_if_not_intermediate_binary_dir()

option(MAHO_BUILD_SHARED "Build Maho engine as a shared library (DLL)" ON)
# Push Maho::FEditorLayer from the game App (Editor sources live in the game project under Source/Editor/).
option(GAME_WITH_EDITOR "Enable Maho::FEditorLayer overlay (disable for shipping)" ON)

# Enabled plugins come from the sibling .cproject (or -DMAHO_CPROJECT=...).
if(NOT MAHO_CPROJECT)
	set(_MAHO_CPROJECT_CANDIDATE "${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}.cproject")
	if(EXISTS "${_MAHO_CPROJECT_CANDIDATE}")
		set(MAHO_CPROJECT "${_MAHO_CPROJECT_CANDIDATE}")
	endif()
	unset(_MAHO_CPROJECT_CANDIDATE)
endif()
if(MAHO_CPROJECT)
	file(TO_CMAKE_PATH "${MAHO_CPROJECT}" MAHO_CPROJECT)
	set(MAHO_CPROJECT "${MAHO_CPROJECT}" CACHE FILEPATH "Game .cproject (enabled Plugins list)" FORCE)
	message(STATUS "Maho: using .cproject for plugins: ${MAHO_CPROJECT}")
endif()

add_subdirectory("${MAHO_ENGINE_ROOT}/Maho" "${CMAKE_BINARY_DIR}/MahoBuild")

set(GAME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(GAME_SOURCE_DIR "${GAME_ROOT}/Source")
set(GAME_SHADERS_DIR "${GAME_ROOT}/Shaders")
set(GAME_PLUGINS_DIR "${GAME_ROOT}/Plugins")
set(GAME_CONFIG_DIR "${GAME_ROOT}/Config")
set(GAME_SCRIPTS_DIR "${GAME_ROOT}/Scripts")

maho_collect_sources(GAME_SOURCES "${GAME_SOURCE_DIR}")
if(GAME_SOURCES STREQUAL "")
	message(FATAL_ERROR "{{PROJECT_NAME}}: no sources under Source/")
endif()

add_executable({{PROJECT_NAME}} ${GAME_SOURCES})
target_include_directories({{PROJECT_NAME}} PRIVATE
	"${GAME_SOURCE_DIR}"
	"${MAHO_ENGINE_ROOT}/Maho/Source/Private"
)
target_link_libraries({{PROJECT_NAME}} PRIVATE Maho::Engine Maho::Modules)
target_compile_features({{PROJECT_NAME}} PRIVATE cxx_std_20)
target_compile_definitions({{PROJECT_NAME}} PRIVATE
	$<$<BOOL:${GAME_WITH_EDITOR}>:GAME_WITH_EDITOR=1>
)

if(MSVC)
	target_compile_options({{PROJECT_NAME}} PRIVATE /W4 /permissive- /Zc:preprocessor /utf-8)
else()
	target_compile_options({{PROJECT_NAME}} PRIVATE -Wall -Wextra -Wpedantic)
endif()

maho_set_output_dirs({{PROJECT_NAME}})
maho_source_group_mirrors("${GAME_SOURCE_DIR}" ${GAME_SOURCES})
maho_add_lua_scripts({{PROJECT_NAME}} "${GAME_SCRIPTS_DIR}")
set_target_properties({{PROJECT_NAME}} PROPERTIES
	FOLDER "Games"
	VS_DEBUGGER_WORKING_DIRECTORY "${GAME_ROOT}"
	WIN32_EXECUTABLE $<BOOL:${WIN32}>
)
add_dependencies({{PROJECT_NAME}} Maho MahoModules)

maho_copy_shaders({{PROJECT_NAME}} "${GAME_SHADERS_DIR}" "Project/Shaders")
if(IS_DIRECTORY "${GAME_CONFIG_DIR}")
	add_custom_command(TARGET {{PROJECT_NAME}} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:{{PROJECT_NAME}}>/Config"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${GAME_CONFIG_DIR}"
			"$<TARGET_FILE_DIR:{{PROJECT_NAME}}>/Config"
		COMMENT "Copy project Config (DefaultEngine.ini)"
		VERBATIM
	)
endif()
if(IS_DIRECTORY "${GAME_SCRIPTS_DIR}")
	add_custom_command(TARGET {{PROJECT_NAME}} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:{{PROJECT_NAME}}>/Scripts"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${GAME_SCRIPTS_DIR}"
			"$<TARGET_FILE_DIR:{{PROJECT_NAME}}>/Scripts"
		COMMENT "Copy project Scripts (Lua)"
		VERBATIM
	)
endif()
add_custom_command(TARGET {{PROJECT_NAME}} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:{{PROJECT_NAME}}>/Project/Plugins"
	COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:{{PROJECT_NAME}}>/Engine/Plugins"
	COMMENT "Ensure plugin runtime folders exist"
	VERBATIM
)
if(IS_DIRECTORY "${MAHO_ENGINE_PLUGINS_DIR}")
	add_custom_command(TARGET {{PROJECT_NAME}} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${MAHO_ENGINE_PLUGINS_DIR}"
			"$<TARGET_FILE_DIR:{{PROJECT_NAME}}>/Engine/Plugins"
		COMMENT "Copy engine plugins"
		VERBATIM
	)
endif()
if(DEFINED MAHO_ENGINE_FONTS_DIR AND IS_DIRECTORY "${MAHO_ENGINE_FONTS_DIR}")
	add_custom_command(TARGET {{PROJECT_NAME}} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:{{PROJECT_NAME}}>/Engine/Fonts"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${MAHO_ENGINE_FONTS_DIR}"
			"$<TARGET_FILE_DIR:{{PROJECT_NAME}}>/Engine/Fonts"
		COMMENT "Copy engine Fonts"
		VERBATIM
	)
endif()
if(IS_DIRECTORY "${GAME_PLUGINS_DIR}")
	add_custom_command(TARGET {{PROJECT_NAME}} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${GAME_PLUGINS_DIR}"
			"$<TARGET_FILE_DIR:{{PROJECT_NAME}}>/Project/Plugins"
		COMMENT "Copy project plugins"
		VERBATIM
	)
endif()

maho_install_runtime_target({{PROJECT_NAME}})
maho_install_directory("${GAME_SHADERS_DIR}" "Project/Shaders")
maho_install_directory("${GAME_PLUGINS_DIR}" "Project/Plugins")
maho_install_directory("${GAME_CONFIG_DIR}" "Config")
maho_install_directory("${GAME_SCRIPTS_DIR}" "Scripts")

# Packaging is done via project package.bat → Tools/package_ui.py (cmake --install).
