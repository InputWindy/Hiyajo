# Third-party dependency helpers for the Maho engine.
#
# The engine core has ZERO third-party libraries. Each plugin pulls its own
# deps via its <Name>.cmake (spdlog → Log, toml++ → Config, nlohmann/json →
# Json, zstd/zlib → Compress, utfcpp → Text, Jolt → Physics, miniaudio →
# Audio, GLM → Math; Vulkan/libktx → Render when migrated).
# This file keeps only the FetchContent reuse helper + the system Threads pkg.

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
			file(REMOVE_RECURSE "${_maho_fc_src}")   # clean any half-clone
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

# Manual git fetch-source override. In mainland China github.com clones often
# hang; set MAHO_GIT_PROXY_PREFIX to a transparent proxy prefix (e.g.
# "https://ghproxy.com/") and every github.com URL below is cloned through it:
#   cmake -DMAHO_GIT_PROXY_PREFIX=https://ghproxy.com/ ...
#   or set the env var MAHO_GIT_PROXY_PREFIX (e.g. `setx MAHO_GIT_PROXY_PREFIX https://ghproxy.com/`).
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

	# Plugin-local mirror config: <plugin>/settings.json → "mirrors" → { owner_repo: url }.
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

	# Proxy prefix fallback (only when the result is still a github.com URL).
	if(MAHO_GIT_PROXY_PREFIX AND _result MATCHES "^https://github\\.com/")
		set(_result "${MAHO_GIT_PROXY_PREFIX}${_result}")
	endif()

	set(${OUT_VAR} "${_result}" PARENT_SCOPE)
endfunction()

# std::thread (FThreadPool / FThreadedServer) needs the system threads library
# on Linux (pthread). Not a third-party library — a system package.
find_package(Threads REQUIRED)
