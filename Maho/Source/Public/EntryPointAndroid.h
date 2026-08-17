#pragma once

/**
 * Android entry shim. Include in exactly one .cpp (the thin Main.cpp).
 * android_main runs on the glue thread; RunDynamic blocks there for the
 * app's lifetime.
 */

#include <EntryPoint.h>

#include <android_native_app_glue.h>

void android_main(struct android_app* App)
{
	(void)App;

	// RunDynamic needs (Argc, Argv); Android has no argv — pass a placeholder.
	char Dummy = '\0';
	char* Argv[] = { &Dummy };
	Maho::RunDynamic(1, Argv);
}
