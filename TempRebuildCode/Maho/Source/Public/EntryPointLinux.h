#pragma once

/**
 * Linux entry shim. Include in exactly one .cpp.
 *
 * A single main drives MahoMain. Console (command line) and windowed builds
 * are the same binary — windowed vs headless is a plugin-selection concern,
 * not an entry-point difference.
 */

#include <EntryPoint.h>

int main(int Argc, char** Argv)
{
	return MahoMain(Argc, Argv);
}
