#pragma once

/**
 * Maho shared app driver (platform-agnostic).
 *
 * This header is included by one of the platform entry shims
 * (EntryPointDesktop / EntryPointAndroid / EntryPointIOS / EntryPointXbox),
 * which in turn is included in exactly one .cpp of the game project.
 *
 * MahoMain drives the whole app: install fatal handlers → create the
 * singleton registry (ctor: ParseCommandLine + Init) → create the engine
 * (ctor: ParseCommandLine) → MainLoop → destroy. Anything that escapes
 * MainLoop funnels into ReportFatal (stderr + Saved/Logs/Fatal.log + abort).
 *
 * The project defines CreateSingletonRegistry() and CreateEngine() (code-gen).
 */

#include <Engine.h>

#include <exception>
#include <memory>

namespace Maho
{

/** Project-defined: create the concrete singleton registry (owning raw pointer). */
FSingletonRegistryBase* CreateSingletonRegistry(int Argc, char** Argv);

/** Project-defined: create the concrete runnable engine (owning raw pointer). */
IRunable* CreateEngine(int Argc, char** Argv);

} // namespace Maho

namespace
{

int MahoMain(int Argc, char** Argv)
{
	// Install fatal handlers before anything else (pre-scheduler).
	Maho::InstallFatalHandlers();

	Maho::IRunable* App = nullptr;
	try
	{
		// RAII: registry ctor runs ParseCommandLine + Init, dtor runs Shutdown.
		std::unique_ptr<Maho::FSingletonRegistryBase> Registry(Maho::CreateSingletonRegistry(Argc, Argv));
		if (!Registry)
		{
			Maho::ReportFatal("CreateSingletonRegistry returned null");
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

} // namespace
