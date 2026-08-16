#pragma once

/**
 * Windows entry shim. Include in exactly one .cpp.
 *
 * WinMain for the GUI subsystem (no console box) + main for the console
 * subsystem. Both drive the same MahoMain — the "command line" vs "window"
 * split is a link-time subsystem choice, not a code difference.
 */

#include <EntryPoint.h>

#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

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
