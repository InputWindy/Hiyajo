#pragma once

// Build configuration -- the axes, read here.
//
// TWO ORTHOGONAL AXES come from the project file (`.cproject`) and are derived into macros by
// `Tools/maho_tools.py` at generate time, which is their ONE definition site:
//
//   BuildType      Runtime | Editor            -> MAHO_EDITOR_BUILD (Editor only)
//   Configuration  Debug | Release | Shipping
//                                             -> MAHO_BUILD_DEBUG / _RELEASE / _SHIPPING
//                                             -> MAHO_DO_*   (behavior: which checks exist)
//                                             -> MAHO_WITH_* (capability: which facilities exist)
//
// The six cells and what each isolates are the spec: `openspec/changes/add-build-configuration/`
// (the matrix is design.md D2/D3). Three rules keep it from drifting back into a pile of ad-hoc
// switches:
//
//   - AXIS macros      (MAHO_BUILD_*, MAHO_EDITOR_BUILD) are never set by hand -- they ARE the axes.
//   - BEHAVIOR macros  (MAHO_DO_*) are derived from the configuration and stay that way.
//   - CAPABILITY macros(MAHO_WITH_*) get a derived default and may be overridden by a project.
//
// Everything below has a fallback for code built OUTSIDE the generator (a tool, an external test
// project). The fallbacks take the "diagnostics on" side deliberately: such a build should behave
// like Debug rather than silently drop the very checks it is being used to verify.

// The generator emits a definition for exactly the configurations it knows; anything else means the
// build is not one of the documented cells.
#if defined(MAHO_BUILD_UNSUPPORTED)
#	error "Unsupported build configuration: the generator knows Debug, Release and Shipping."
#endif

// Shipping x Editor contradicts itself: an editor needs the very facilities Shipping removes.
// Ship an editor with Release x Editor instead.
#if defined(MAHO_EDITOR_BUILD) && defined(MAHO_BUILD_SHIPPING)
#	error "Shipping x Editor is not a valid combination -- ship an editor with Release x Editor."
#endif

// -- behavior: which checks exist -------------------------------------------------------

#ifndef MAHO_DO_CHECK
#	define MAHO_DO_CHECK 1
#endif

#ifndef MAHO_DO_ENSURE
#	define MAHO_DO_ENSURE 1
#endif

#ifndef MAHO_DO_SLOW_CHECK
#	define MAHO_DO_SLOW_CHECK 1
#endif

#ifndef MAHO_DO_CONTAINER_CHECKS
#	define MAHO_DO_CONTAINER_CHECKS 1
#endif

// -- capability: which facilities exist -------------------------------------------------

#ifndef MAHO_WITH_TRACE
#	define MAHO_WITH_TRACE 1
#endif

#ifndef MAHO_WITH_LOGGING
#	define MAHO_WITH_LOGGING 1
#endif

#ifndef MAHO_WITH_CRASH_REPORT
#	define MAHO_WITH_CRASH_REPORT 1
#endif

#ifndef MAHO_WITH_RHI_VALIDATION
#	define MAHO_WITH_RHI_VALIDATION 1
#endif

#ifndef MAHO_WITH_GLSLANG
#	define MAHO_WITH_GLSLANG 1
#endif

#ifndef MAHO_WITH_STATS
#	define MAHO_WITH_STATS 1
#endif

namespace Maho
{

/** The cell this translation unit was built as, for a report line or a crash header. Compiled from
 *  the macros above, so it can never disagree with what the code actually did. */
struct FBuildInfo
{
	static constexpr const char* Config =
	#if defined(MAHO_BUILD_SHIPPING)
		"Shipping";
	#elif defined(MAHO_BUILD_RELEASE)
		"Release";
	#else
		"Debug";
	#endif

	static constexpr const char* Target =
	#if defined(MAHO_EDITOR_BUILD)
		"Editor";
	#else
		"Runtime";
	#endif
};

} // namespace Maho
