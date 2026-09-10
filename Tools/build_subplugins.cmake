# Maho sub-plugin build trigger (script mode: cmake -P <this file>).
#
# Runs from a parent plugin's POST_BUILD step: after the parent has linked, build
# the sub-plugins it declares in its .cplugin "Plugins" field -- so a single-target
# build (VS Shift+F6 on the parent) still refreshes every sub-plugin DLL it will
# install at runtime, instead of leaving a stale one in Binaries/<Config>.
#
# Why a script and not add_dependencies(parent, child): a sub-plugin LINKS its
# parent (e.g. Scene -> Render), which is already a reverse target edge, so a
# parent->child target dependency closes a cycle and CMake refuses to generate
# ("Cyclic dependencies are allowed only among static libraries"). This step is a
# build-script action, not a dependency -- it only runs after the parent is linked,
# which is also exactly the order the sub-plugin needs for its import lib.
#
# Inputs (all passed with -D):
#   MAHO_BUILD_DIR            - the project build tree (CMAKE_BINARY_DIR of the caller)
#   MAHO_CONFIG               - $<CONFIG> of the caller
#   MAHO_TARGETS              - the parent's generated <Parent>_SubPlugins handle
#                               target (it depends on every enabled sub-plugin),
#                               so the whole closure builds in ONE msbuild run
#   MAHO_IN_SOLUTION_BUILD    - $(BuildingSolutionFile) of the MSBuild run (may be unset)
#   MAHO_SUBPLUGIN_BUILD      - $(MahoSubPluginBuild) of the MSBuild run (may be unset;
#                               the nested build sets it, so this script no-ops there)

# A solution / all-target build already builds every project, and nesting msbuild
# into a build that is itself building those projects would have two processes
# writing the same outputs. Only an explicit "true" skips (an unexpanded
# $(BuildingSolutionFile) must not silently disable this step).
string(TOLOWER "${MAHO_IN_SOLUTION_BUILD}" _MahoInSolution)
if(_MahoInSolution STREQUAL "true")
	return()
endif()

# Not covered by that gate: an all-target build from the command line
# (`cmake --build <dir>` == msbuild ALL_BUILD.vcxproj) reports no
# $(BuildingSolutionFile), so the step nests once per parent even though that
# outer build already builds every sub-plugin. Redundant, but harmless: the
# nested Maho target sees MAHO_SUBPLUGIN_BUILD and skips its clean, so the outer
# build's fresh DLLs survive and MSBuild's up-to-date check reuses them.

# Re-entrancy guard: the nested build below runs MSBuild on individual .vcxproj
# files, so their own POST_BUILD steps would nest again. The marker is the
# MahoSubPluginBuild MSBuild property that the nested build passes on its own
# command line (/p:MahoSubPluginBuild=1) -- it reaches here as -DMAHO_SUBPLUGIN_BUILD
# and is what the Maho target's PRE_BUILD event tests to skip its Binaries clean
# (that clean would otherwise delete the DLLs the outer build just produced and
# relink the whole chain a second time). An environment variable is not usable:
# MSBuild runs a pre-build event in a reused worker node whose environment
# predates the build. A file marker is not usable either: MSBuild re-encodes a
# pre-build event's command line, so an absolute path holding non-ASCII
# characters (this tree does) arrives at cmd mangled.
if(NOT MAHO_BUILD_DIR)
	message(FATAL_ERROR "Maho: build_subplugins.cmake needs -DMAHO_BUILD_DIR=<build tree>")
endif()

if(MAHO_SUBPLUGIN_BUILD STREQUAL "1")
	return()
endif()
if(NOT MAHO_TARGETS)
	return()
endif()

string(REPLACE "," ";" _MahoTargets "${MAHO_TARGETS}")
message(STATUS "Maho: building sub-plugins - ${_MahoTargets}")

# The nested build must be recognised as such by the projects it rebuilds: the
# VS generator forwards everything after "--" to msbuild, so the property is set
# for every project of that run. Other generators (make/ninja) get the flag
# nowhere; set MAHO_SUBPLUGIN_BUILD in their environment instead.
set(_MahoNestedArgs "")
if(WIN32)
	list(APPEND _MahoNestedArgs -- /p:MahoSubPluginBuild=1)
else()
	set(ENV{MAHO_SUBPLUGIN_BUILD} "1")
endif()

execute_process(
	COMMAND "${CMAKE_COMMAND}" --build "${MAHO_BUILD_DIR}"
		--target ${_MahoTargets}
		--config "${MAHO_CONFIG}"
		${_MahoNestedArgs}
	RESULT_VARIABLE _MahoSubPluginResult
)

if(NOT _MahoSubPluginResult EQUAL 0)
	message(FATAL_ERROR
		"Maho: building sub-plugins (${_MahoTargets}) failed with exit code ${_MahoSubPluginResult}")
endif()
