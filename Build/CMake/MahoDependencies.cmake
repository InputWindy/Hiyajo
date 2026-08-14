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

# GLFW lives in the Platform plugin (Maho/Plugins/Platform/Platform.cmake).
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

# nlohmann/json + glm live in Maho/Maho.cmake (FetchContent → build Intermediate).

# Engine fonts / editor assets live under Maho/Content (copied next to the binary).
set(MAHO_ENGINE_FONTS_DIR "${_MAHO_REPO_ROOT}/Maho/Content/Fonts" CACHE INTERNAL "Engine icon/UI fonts")
set(MAHO_ENGINE_EDITOR_DIR "${_MAHO_REPO_ROOT}/Maho/Content/Editor" CACHE INTERNAL "Engine editor assets (wallpaper, …)")

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

unset(_MAHO_REPO_ROOT)
unset(_MAHO_PUBLIC_HEADERS)
unset(_MAHO_TP)
