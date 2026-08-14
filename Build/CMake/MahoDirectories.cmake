# UE-style workspace directories + platform id.
# Intermediate  = compile/link intermediates + CMake/VS project files (-B Intermediate)
# Binaries      = daily-run executables / DLLs (per platform)
# Cached        = derived data (shader cache, DDC-like) — regenerable
# Saved         = logs / config / crashes / screenshots — user & session data
# Packaged      = clean shipping tree (exe + required resources only)

# Prefer an explicit workspace root (engine Build/CMakeLists sets this to repo root).
# Game projects leave it unset so CMAKE_SOURCE_DIR (the .cproject folder) is used.
if(NOT DEFINED MAHO_WORKSPACE_ROOT OR "${MAHO_WORKSPACE_ROOT}" STREQUAL "")
	set(MAHO_WORKSPACE_ROOT "${CMAKE_SOURCE_DIR}")
endif()
get_filename_component(MAHO_WORKSPACE_ROOT "${MAHO_WORKSPACE_ROOT}" ABSOLUTE)

if(WIN32)
	set(MAHO_PLATFORM_NAME "Win64")
	set(MAHO_PLATFORM_WINDOWS TRUE)
elseif(APPLE)
	set(MAHO_PLATFORM_NAME "Mac")
	set(MAHO_PLATFORM_MAC TRUE)
else()
	set(MAHO_PLATFORM_NAME "Linux")
	set(MAHO_PLATFORM_LINUX TRUE)
endif()

set(MAHO_INTERMEDIATE_DIR "${MAHO_WORKSPACE_ROOT}/Maho/Intermediate")
set(MAHO_BINARIES_DIR     "${MAHO_WORKSPACE_ROOT}/Binaries/${MAHO_PLATFORM_NAME}")
set(MAHO_CACHED_DIR       "${MAHO_WORKSPACE_ROOT}/Cached")
set(MAHO_SAVED_DIR        "${MAHO_WORKSPACE_ROOT}/Saved")
set(MAHO_PACKAGED_DIR     "${MAHO_WORKSPACE_ROOT}/Packaged/${MAHO_PLATFORM_NAME}")

set(MAHO_BIN_DIR "${MAHO_BINARIES_DIR}")
set(MAHO_LIB_DIR "${MAHO_INTERMEDIATE_DIR}/Lib/${MAHO_PLATFORM_NAME}")

function(maho_ensure_workspace_dirs)
	foreach(_dir
		"${MAHO_BINARIES_DIR}"
		"${MAHO_CACHED_DIR}/Shaders"
		"${MAHO_CACHED_DIR}/DerivedData"
		"${MAHO_SAVED_DIR}/Logs"
		"${MAHO_SAVED_DIR}/Config"
		"${MAHO_SAVED_DIR}/Crashes"
		"${MAHO_SAVED_DIR}/Screenshots"
		"${MAHO_PACKAGED_DIR}"
	)
		file(MAKE_DIRECTORY "${_dir}")
	endforeach()
endfunction()

function(maho_warn_if_not_intermediate_binary_dir)
	file(TO_CMAKE_PATH "${CMAKE_BINARY_DIR}" _bin)
	file(TO_CMAKE_PATH "${MAHO_INTERMEDIATE_DIR}" _want)
	if(NOT _bin STREQUAL _want)
		message(WARNING
			"CMAKE_BINARY_DIR is:\n  ${_bin}\n"
			"UE-style layout expects Intermediate as the CMake binary dir:\n"
			"  generateProject.bat\n"
			"  (or cmake -S Build -B Intermediate ...)"
		)
	endif()
endfunction()
