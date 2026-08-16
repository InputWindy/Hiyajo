#pragma once

/**
 * Xbox entry shim (Microsoft GDK / GameCore). Compile only when building for
 * Xbox; include in exactly one .cpp.
 *
 * GDK apps use a standard main. XGameRuntimeInitialize/Uninitialize is left
 * to the project's bootstrap (or a later GDK-specific shim); this shim just
 * drives MahoMain.
 */

#include <EntryPoint.h>

int main(int Argc, char** Argv)
{
	return MahoMain(Argc, Argv);
}
