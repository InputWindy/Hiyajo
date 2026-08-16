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
				message(STATUS "Maho: Downloading ${_name} from the network (first time)…")
				FetchContent_Populate(${_name})
			endif()
		unset(_maho_fc_src)
		unset(_maho_fc_ok)
		unset(_maho_fc_lower)
		unset(_maho_fc_marker)
	endif()
endmacro()

# std::thread (FThreadPool / FThreadedServer) needs the system threads library
# on Linux (pthread). Not a third-party library — a system package.
find_package(Threads REQUIRED)
