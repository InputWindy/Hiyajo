#pragma once

/**
 * Xbox entry shim. Include in exactly one .cpp (the thin Main.cpp).
 * A single main driving Maho::RunDynamic.
 */

#include <EntryPoint.h>

int main(int Argc, char** Argv)
{
	return Maho::RunDynamic(Argc, Argv);
}
