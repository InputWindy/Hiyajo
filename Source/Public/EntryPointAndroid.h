#pragma once

/**
 * Android entry shim. Include in exactly one .cpp (the thin Main.cpp).
 * android_main runs on the glue thread; Main blocks there for the
 * app's lifetime.
 */

#include <EntryPoint.h>

#include <android_native_app_glue.h>

void android_main(struct android_app* App)
{
	(void)App;

	// Main needs (Argc, Argv); Android has no argv — pass a placeholder.
	char Dummy = '\0';
	char* Argv[] = { &Dummy };
	Maho::Main(1, Argv);
}
