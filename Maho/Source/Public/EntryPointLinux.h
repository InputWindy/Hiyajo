#pragma once

/**
 * Linux entry shim. Include in exactly one .cpp.
 *
 * A single main. IDE drives MahoMain; define MAHO_CLI_ENTRY before including
 * to drive MahoCLIMain instead.
 */

#include <EntryPoint.h>

#if defined(MAHO_CLI_ENTRY)

int main(int Argc, char** Argv)
{
	return MahoCLIMain(Argc, Argv);
}

#else

int main(int Argc, char** Argv)
{
	return MahoMain(Argc, Argv);
}

#endif
