# std::thread (FThreadPool / FThreadedServer) needs the system threads library
# on Linux (pthread). Not a third-party library — a system package.
find_package(Threads REQUIRED)

# ── FetchContent reuse helper ───────────────────────────────────────────
include(FetchContent)

# VS rebuilds re-run CMake when this file changes. Skip git "update" so offline /
# flaky GitHub access does not break an already-populated Intermediate/_deps tree.
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL
	"Skip FetchContent git update when already populated" FORCE)

# If a previous populate left sources but lost CMake stamps, reuse the tree.
# Otherwise clone manually (execute_process WITHOUT output capture) so git's
# --progress streams live to the console — FetchContent_Populate captures
# git's output into variables, hiding the progress.
# Usage: maho_fetchcontent_populate_or_reuse(<name> <repo-url> <git-tag> <marker>...)
macro(maho_fetchcontent_populate_or_reuse _name _repo _tag)
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
			message(STATUS "Maho: reusing ${_name} at ${_maho_fc_src}")
		else()
			message(STATUS "Maho: Downloading ${_name} (${_repo}) …")
			find_package(Git REQUIRED)
			file(REMOVE_RECURSE "${_maho_fc_src}")
			execute_process(
				COMMAND "${GIT_EXECUTABLE}" clone --progress --depth 1 --branch "${_tag}" "${_repo}" "${_maho_fc_src}"
				RESULT_VARIABLE _maho_fc_result
			)
			if(NOT _maho_fc_result EQUAL 0)
				message(FATAL_ERROR "Maho: git clone failed for ${_name} (exit ${_maho_fc_result})")
			endif()
		endif()
		set(${_name}_SOURCE_DIR "${_maho_fc_src}")
		set(${_name}_BINARY_DIR "${FETCHCONTENT_BASE_DIR}/${_maho_fc_lower}-build")
		set(${_name}_POPULATED TRUE)
		unset(_maho_fc_src)
		unset(_maho_fc_ok)
		unset(_maho_fc_lower)
		unset(_maho_fc_marker)
	endif()
endmacro()

# Manual git fetch-source override (github.com clones often hang in mainland
# China). Set MAHO_GIT_PROXY_PREFIX to a transparent proxy prefix.
if(NOT DEFINED MAHO_GIT_PROXY_PREFIX AND DEFINED ENV{MAHO_GIT_PROXY_PREFIX})
	set(MAHO_GIT_PROXY_PREFIX "$ENV{MAHO_GIT_PROXY_PREFIX}")
endif()
set(MAHO_GIT_PROXY_PREFIX "${MAHO_GIT_PROXY_PREFIX}" CACHE STRING
	"Prefix prepended to github.com git clone URLs (empty = clone github.com directly)")

# Rewrite a github.com clone URL: first consult the calling plugin's
# settings.json "mirrors" map (plugin-local, self-contained), then fall back
# to the MAHO_GIT_PROXY_PREFIX proxy.
function(maho_git_repository_url OUT_VAR INPUT_URL)
	set(_result "${INPUT_URL}")

	if(INPUT_URL MATCHES "^https://github\\.com/([^/]+)/([^/]+)(\\.git)?$")
		set(_gh_key "${CMAKE_MATCH_1}_${CMAKE_MATCH_2}")
		if(DEFINED _MOD_PLUGIN_DIR AND EXISTS "${_MOD_PLUGIN_DIR}/settings.json")
			file(READ "${_MOD_PLUGIN_DIR}/settings.json" _settings_json)
			string(JSON _mirror_url ERROR_VARIABLE _mirror_err GET "${_settings_json}" "mirrors" "${_gh_key}")
			if(_mirror_err STREQUAL "" AND NOT _mirror_url STREQUAL "")
				set(_result "${_mirror_url}")
			endif()
		endif()
	endif()

	if(MAHO_GIT_PROXY_PREFIX AND _result MATCHES "^https://github\\.com/")
		set(_result "${MAHO_GIT_PROXY_PREFIX}${_result}")
	endif()

	set(${OUT_VAR} "${_result}" PARENT_SCOPE)
endfunction()

# Recursively collect every BUILDSYSTEM_TARGETS entry under a directory (the
# property is per-directory, so subdirectories must be walked by hand).
function(maho_list_targets_recursive _dir _out)
	get_property(_local DIRECTORY "${_dir}" PROPERTY BUILDSYSTEM_TARGETS)
	set(_all ${_local})
	get_property(_subdirs DIRECTORY "${_dir}" PROPERTY SUBDIRECTORIES)
	foreach(_sub IN LISTS _subdirs)
		maho_list_targets_recursive("${_sub}" _sub_targets)
		list(APPEND _all ${_sub_targets})
	endforeach()
	set(${_out} "${_all}" PARENT_SCOPE)
endfunction()

# add_subdirectory for a third-party dep, then group every target it defines
# under the VS "ThirdParty" solution folder (keeps the sln tree clean without
# moving the sources out of Intermediate/_deps).
macro(maho_add_thirdparty_subdirectory)
	set_property(GLOBAL PROPERTY USE_FOLDERS ON)
	maho_list_targets_recursive("${CMAKE_CURRENT_SOURCE_DIR}" _maho_tp_before)
	add_subdirectory(${ARGN})
	maho_list_targets_recursive("${CMAKE_CURRENT_SOURCE_DIR}" _maho_tp_after)
	list(REMOVE_ITEM _maho_tp_after ${_maho_tp_before})
	foreach(_maho_tp_target IN LISTS _maho_tp_after)
		string(REGEX REPLACE "^BUILDSYSTEM_TARGET_" "" _maho_tp_name "${_maho_tp_target}")
		if(NOT _maho_tp_name STREQUAL "")
			get_target_property(_maho_tp_type "${_maho_tp_name}" TYPE)
			if(NOT _maho_tp_type STREQUAL "INTERFACE_LIBRARY")
				set_target_properties("${_maho_tp_name}" PROPERTIES FOLDER "ThirdParty")
			endif()
		endif()
	endforeach()
	unset(_maho_tp_before)
	unset(_maho_tp_after)
	unset(_maho_tp_target)
	unset(_maho_tp_name)
	unset(_maho_tp_type)
endmacro()

# ── Engine-level third-party (fetched ONCE, usable by every target) ──────
# These are the generic infrastructure libs the engine provides to all code
# (like std). Domain-specific libs still go through per-plugin .cmake files.

# GLM (header-only math).
if(NOT TARGET glm::glm)
	maho_git_repository_url(_M_GLM_URL https://github.com/g-truc/glm.git)
	maho_fetchcontent_populate_or_reuse(glm ${_M_GLM_URL} 0.9.9.8 glm/glm.hpp)
	add_library(glm::glm INTERFACE IMPORTED)
	set_target_properties(glm::glm PROPERTIES
		INTERFACE_INCLUDE_DIRECTORIES "${glm_SOURCE_DIR}"
		INTERFACE_COMPILE_DEFINITIONS "GLM_ENABLE_EXPERIMENTAL"
	)
	unset(_M_GLM_URL)
endif()

# nlohmann/json (header-only).
if(NOT TARGET nlohmann_json::nlohmann_json)
	maho_git_repository_url(_M_NLJSON_URL https://github.com/nlohmann/json.git)
	maho_fetchcontent_populate_or_reuse(nlohmann_json ${_M_NLJSON_URL} v3.11.3 single_include/nlohmann/json.hpp)
	add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
	set_target_properties(nlohmann_json::nlohmann_json PROPERTIES
		INTERFACE_INCLUDE_DIRECTORIES "${nlohmann_json_SOURCE_DIR}/single_include")
	unset(_M_NLJSON_URL)
endif()