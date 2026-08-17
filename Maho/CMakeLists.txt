cmake_minimum_required(VERSION 3.20)

set(MAHO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(MAHO_SOURCE_DIR "${MAHO_ROOT}/Source")
set(MAHO_PUBLIC_DIR "${MAHO_SOURCE_DIR}/Public")
set(MAHO_PRIVATE_DIR "${MAHO_SOURCE_DIR}/Private")
set(MAHO_PLUGINS_DIR "${MAHO_ROOT}/Plugins")

# Resolve Build/CMake helpers when Maho is added from engine workspace or external .cproject.
if(NOT DEFINED MAHO_ENGINE_BUILD_CMAKE_DIR)
	get_filename_component(MAHO_ENGINE_BUILD_CMAKE_DIR "${MAHO_ROOT}/../Build/CMake" ABSOLUTE)
endif()
list(APPEND CMAKE_MODULE_PATH "${MAHO_ENGINE_BUILD_CMAKE_DIR}")
include(MahoDependencies)
include(MahoHelpers)
include(Maho.cmake)

# Engine repo root + local Python (used by plugin scan tooling).
get_filename_component(_MAHO_ENGINE_ROOT "${MAHO_ROOT}/.." ABSOLUTE)
set(_MAHO_LOCAL_PYTHON "${_MAHO_ENGINE_ROOT}/Tools/python/python.exe")
if(NOT EXISTS "${_MAHO_LOCAL_PYTHON}")
	set(_MAHO_LOCAL_PYTHON "${_MAHO_ENGINE_ROOT}/Tools/python/Scripts/python.exe")
endif()

maho_collect_sources(MAHO_SOURCES "${MAHO_SOURCE_DIR}")

if(MAHO_SOURCES STREQUAL "")
	message(FATAL_ERROR "Maho: no sources found under ${MAHO_SOURCE_DIR}")
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

target_include_directories(Maho
	PUBLIC
		"${MAHO_PUBLIC_DIR}"
	PRIVATE
		"${MAHO_PRIVATE_DIR}"
)

# Engine core links only the system threads + dl libraries (std::thread,
# LoadLibrary/dlopen). All third-party libraries live in their plugins.
target_link_libraries(Maho
	PRIVATE
		Threads::Threads
		${CMAKE_DL_LIBS}
)

# Absolute engine root for locating Tools/AgentBridge at runtime (dev builds).
file(TO_CMAKE_PATH "${_MAHO_ENGINE_ROOT}" _MAHO_ENGINE_ROOT_PATH)
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
set_target_properties(Maho PROPERTIES
	FOLDER "Engine"
	OUTPUT_NAME "Maho"
)

# Preserve -DMAHO_ENGINE_ROOT from game projects (repo root). Never overwrite with Maho/.
if(NOT MAHO_ENGINE_ROOT)
	get_filename_component(MAHO_ENGINE_ROOT "${MAHO_ROOT}/.." ABSOLUTE)
endif()
set(MAHO_ENGINE_PLUGINS_DIR "${MAHO_PLUGINS_DIR}" CACHE INTERNAL "Maho engine plugins")

maho_install_runtime_target(Maho)
maho_install_directory("${MAHO_PLUGINS_DIR}" "Engine/Plugins")

set(_MAHO_SCAN_PLUGINS_PY "${_MAHO_ENGINE_ROOT}/Tools/scan_plugins.py")
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
