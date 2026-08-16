#pragma once

/**
 * Android entry shim (NDK native_app_glue). Compile only when cross-building
 * for Android; include in exactly one .cpp.
 *
 * android_main runs on the glue thread. MahoMain blocks there until the
 * Android platform extension calls RequestShutdown on APP_CMD_DESTROY.
 * The Android platform extension reads Maho::GAndroidApp for the app state.
 */

#include <EntryPoint.h>

#include <android_native_app_glue.h>

namespace Maho
{

/** Set by android_main; read by the Android platform extension. */
inline struct android_app* GAndroidApp = nullptr;

} // namespace Maho

void android_main(struct android_app* App)
{
	Maho::GAndroidApp = App;

	// Android has no argc/argv — synthesize an empty command line.
	char Arg0[] = "Maho";
	char* Argv[] = { Arg0, nullptr };
	MahoMain(1, Argv);
}
