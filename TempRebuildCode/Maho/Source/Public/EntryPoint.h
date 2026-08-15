#pragma once

/**
 * Platform entry point for Maho game executables.
 * Include this header in exactly one .cpp of the game project.
 *
 * Exception handling is installed first (InstallFatalHandlers) — before any
 * scheduler or engine object exists. The main-thread lifecycle is wrapped in
 * try/catch: anything that escapes MainLoop funnels into ReportFatal (stderr
 * + Saved/Logs/Fatal.log + abort).
 *
 * The project defines CreateSingletonRegistry() and CreateApp() (code-gen).
 *
 * On Windows the game is typically linked as a GUI app (WIN32_EXECUTABLE) so
 * no console black box appears.
 */

#include <Engine.h>

#include <exception>
#include <memory>

namespace Maho
{

/** Project-defined: create the concrete singleton registry (owning raw pointer). */
FSingletonRegistryBase* CreateSingletonRegistry();

/** Project-defined: create the concrete runnable engine (owning raw pointer). */
IRunable* CreateApp();

} // namespace Maho

namespace
{

int MahoMain(int Argc, char** Argv)
{
	(void)Argc;
	(void)Argv;

	// Install fatal handlers before anything else (pre-scheduler).
	Maho::InstallFatalHandlers();

	Maho::IRunable* App = nullptr;
	try
	{
		// RAII: registry ctor runs Init, dtor runs Shutdown.
		std::unique_ptr<Maho::FSingletonRegistryBase> Registry(Maho::CreateSingletonRegistry());
		if (!Registry)
		{
			Maho::ReportFatal("CreateSingletonRegistry returned null");
		}

		App = Maho::CreateApp();
		if (!App)
		{
			Maho::ReportFatal("CreateApp returned null");
		}

		App->MainLoop();
		delete App;
		App = nullptr;
		return 0;
	}
	catch (const std::exception& Exception)
	{
		delete App;
		Maho::ReportFatal(Exception.what());
	}
	catch (...)
	{
		delete App;
		Maho::ReportFatal("Unhandled unknown exception");
	}
}

} // namespace

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
