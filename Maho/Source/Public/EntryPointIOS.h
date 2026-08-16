#pragma once

/**
 * iOS entry shim. The Objective-C entry (app delegate) lives in the Xcode
 * project — this header only provides the C++ side: a context pointer for
 * the iOS platform extension + RunIOS() to drive the Maho app.
 *
 * The .mm entry includes this header and calls Maho::RunIOS() once the
 * UIWindow is ready.
 */

#include <EntryPoint.h>

namespace Maho
{

/** iOS app context (UIApplication*) — set by the ObjC entry, read by the iOS platform extension. */
inline void* GIOSApplication = nullptr;

/** Drive the Maho app. Blocks until the iOS platform extension requests shutdown. */
inline int RunIOS()
{
	// iOS has no argc/argv — synthesize an empty command line.
	char Arg0[] = "Maho";
	char* Argv[] = { Arg0, nullptr };
	return MahoMain(1, Argv);
}

} // namespace Maho
