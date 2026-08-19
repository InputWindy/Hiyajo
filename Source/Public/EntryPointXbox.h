#pragma once

/**
 * Xbox entry shim. Include in exactly one .cpp (the thin Main.cpp).
 * A single main driving Maho::Main.
 */

#include <EntryPoint.h>

int main(int Argc, char** Argv)
{
	return Maho::Main(Argc, Argv);
}
