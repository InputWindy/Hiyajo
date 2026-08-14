cmake_minimum_required(VERSION 3.20)

# Engine workspace entry (kept under Build/ so repo root stays clean).
# generateProject.bat → cmake -S Build -B Intermediate

get_filename_component(MAHO_WORKSPACE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

project(MahoWorkspace
	VERSION 0.1.0
	DESCRIPTION "Maho engine workspace (Engine DLL + Game EXE)"
	LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")
include(MahoDirectories)
include(MahoHelpers)

maho_ensure_workspace_dirs()
maho_warn_if_not_intermediate_binary_dir()

option(MAHO_BUILD_SHARED "Build Maho engine as a shared library (DLL)" ON)
option(MAHO_BUILD_WORLD_ADAPTER "Build the optional Maho World Adapter service" OFF)
option(MAHO_BUILD_WORLD_ADAPTER_TESTS "Build the optional Maho World Adapter tests" OFF)

if(MAHO_BUILD_WORLD_ADAPTER_TESTS)
	set(MAHO_BUILD_WORLD_ADAPTER ON CACHE BOOL
		"Build the optional Maho World Adapter service" FORCE)
	enable_testing()
endif()

add_subdirectory("${MAHO_WORKSPACE_ROOT}/Maho" "${CMAKE_BINARY_DIR}/Maho")

if(MAHO_BUILD_WORLD_ADAPTER)
	add_subdirectory(
		"${MAHO_WORKSPACE_ROOT}/Maho/WorldAdapter"
		"${CMAKE_BINARY_DIR}/MahoWorldAdapter"
	)
endif()

# Optional in-tree sample game (may be absent when using external .cproject games).
if(EXISTS "${MAHO_WORKSPACE_ROOT}/Test0/CMakeLists.txt")
	add_subdirectory("${MAHO_WORKSPACE_ROOT}/Test0" "${CMAKE_BINARY_DIR}/Test0")
	set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Test0)
else()
	set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Maho)
endif()

# Packaging is done via Tools/package_ui.py (cmake --install), not a VS solution target.

message(STATUS "=== Maho Workspace (UE-style dirs) ===")
message(STATUS "  Workspace   : ${MAHO_WORKSPACE_ROOT}")
message(STATUS "  Platform    : ${MAHO_PLATFORM_NAME}")
message(STATUS "  Intermediate: ${MAHO_INTERMEDIATE_DIR}")
message(STATUS "  Binaries    : ${MAHO_BINARIES_DIR}/<Config>")
message(STATUS "  Packaged    : ${MAHO_PACKAGED_DIR}")
message(STATUS "Configure via: generateProject.bat")
message(STATUS "Package via  : package.bat")
