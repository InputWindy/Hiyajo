#pragma once

/**
 * Desktop entry shim (Windows + Linux). Include in exactly one .cpp.
 *
 * Windows: WinMain for the GUI subsystem (no console box) + main for the
 * console subsystem. Linux: main only. The console build is the "command
 * line" mode; the GUI build is the "window" mode — the same MahoMain.
 */

#include <EntryPoint.h>

#if defined(_WIN32)
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	include <Windows.h>

int WINAPI WinMain(HINSTANCE /*Instance*/, HINSTANCE /*Prev*/, LPSTR /*CmdLine*/, int /*Show*/)
{
	// Detach any inherited / debugger console so the OS black box stays hidden.
	FreeConsole();

	return MahoMain(__argc, __argv);
}
#endif

int main(int Argc, char** Argv)
{
	return MahoMain(Argc, Argv);
}
