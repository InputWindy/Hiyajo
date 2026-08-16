#pragma once

/**
 * Maho shared app drivers (platform-agnostic).
 *
 * MahoMain drives an IDE/engine app: install fatal handlers → create the
 * toolkit (ctor: ParseCommandLine + Init) → create the engine (ctor:
 * ParseCommandLine) → MainLoop → destroy.
 *
 * MahoCLIMain drives a CLI app: install fatal handlers → create the toolkit
 * (ctor: ParseCommandLine + Init) → return → dtor runs Shutdown.
 *
 * Anything that escapes funnels into ReportFatal (stderr + Fatal.log + abort).
 */

#include <Engine.h>

#include <exception>
#include <memory>

namespace Maho
{

/** Project-defined: create the concrete toolkit (owning raw pointer). */
FToolkitBase* CreateToolkit(int Argc, char** Argv);

/** Project-defined: create the concrete runnable engine (owning raw pointer). */
IRunable* CreateEngine(int Argc, char** Argv);

} // namespace Maho

namespace
{

inline int MahoMain(int Argc, char** Argv)
{
	// Install fatal handlers before anything else (pre-scheduler).
	Maho::InstallFatalHandlers();

	Maho::IRunable* App = nullptr;
	try
	{
		// RAII: registry ctor runs ParseCommandLine + Init, dtor runs Shutdown.
		std::unique_ptr<Maho::FToolkitBase> Registry(Maho::CreateToolkit(Argc, Argv));
		if (!Registry)
		{
			Maho::ReportFatal("CreateToolkit returned null");
		}

		App = Maho::CreateEngine(Argc, Argv);
		if (!App)
		{
			Maho::ReportFatal("CreateEngine returned null");
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

inline int MahoCLIMain(int Argc, char** Argv)
{
	// Install fatal handlers before anything else (pre-scheduler).
	Maho::InstallFatalHandlers();

	try
	{
		// RAII: registry ctor runs ParseCommandLine + Init, dtor runs Shutdown.
		std::unique_ptr<Maho::FToolkitBase> Registry(Maho::CreateToolkit(Argc, Argv));
		if (!Registry)
		{
			Maho::ReportFatal("CreateToolkit returned null");
		}

		return 0;
	}
	catch (const std::exception& Exception)
	{
		Maho::ReportFatal(Exception.what());
	}
	catch (...)
	{
		Maho::ReportFatal("Unhandled unknown exception");
	}
}


} // namespace
