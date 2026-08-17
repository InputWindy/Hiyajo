#pragma once

/**
 * Windows entry shim. Include in exactly one .cpp (the thin Main.cpp).
 * IDE: WinMain for the GUI subsystem (no console box) + main for the console
 * subsystem — both drive Maho::RunDynamic.
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

	return Maho::RunDynamic(__argc, __argv);
}

int main(int Argc, char** Argv)
{
	return Maho::RunDynamic(Argc, Argv);
}
