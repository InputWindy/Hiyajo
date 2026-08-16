#pragma once

/**
 * Windows entry shim. Include in exactly one .cpp.
 *
 * IDE: WinMain for the GUI subsystem (no console box) + main for the console
 * subsystem — both drive MahoMain.
 * CLI: define MAHO_CLI_ENTRY before including — only main, driving MahoCLIMain
 * (console subsystem, no WinMain).
 */

#include <EntryPoint.h>

#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

#if defined(MAHO_CLI_ENTRY)

int main(int Argc, char** Argv)
{
	return MahoCLIMain(Argc, Argv);
}

#else

int WINAPI WinMain(HINSTANCE /*Instance*/, HINSTANCE /*Prev*/, LPSTR /*CmdLine*/, int /*Show*/)
{
	// Detach any inherited / debugger console so the OS black box stays hidden.
	FreeConsole();

	return MahoMain(__argc, __argv);
}

int main(int Argc, char** Argv)
{
	return MahoMain(Argc, Argv);
}

#endif
