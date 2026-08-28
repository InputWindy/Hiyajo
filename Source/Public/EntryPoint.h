#pragma once

#include <Core/Assembly.h>
#include <Core/Fatal.h>
#include <Engine/Engine.h>

namespace Maho
{

/**
 * Unified app driver — install an engine DLL and execute its root instance.
 *
 * No engine/tool preset: the engine is a self-contained DLL exporting
 * `CreateEngine()` → an IEngine*. The entry point loads it via FAssembly,
 * brings the anonymous root up (Initialize), forwards to its main capability,
 * then takes it down symmetrically (Shutdown + dtor teardown).
 *
 *   main()/WinMain() → Maho::Main(Argc, Argv)
 *     InstallFatalHandlers()
 *     FAssembly Load(argv[1])          // install
 *     CreateEngine() → IEngine*        // create the root instance (anonymous)
 *     Initialize(Argc, Argv)           // bring the engine up
 *     Main()                           // execute
 *     Shutdown()                       // symmetric teardown
 *     delete App
 */
inline int Main(int Argc, char** Argv)
{
	InstallFatalHandlers();

#ifndef MAHO_ENGINE_NAME
#	define MAHO_ENGINE_NAME "Engine.dll"
#endif
	// The engine path — first command-line argument, else the project's DLL.
	const char* EnginePath = (Argc > 1 && Argv[1] != nullptr) ? Argv[1] : MAHO_ENGINE_NAME;

	// Install (load) the engine.
	FAssembly Engine(EnginePath);
	if (!Engine.IsLoaded())
	{
		ReportFatal("Failed to install engine assembly");
	}

	// Create the root instance — the exported CreateEngine factory.
	using CreateFunction = IEngine* (*)();
	auto Create = Engine.GetProcAs<CreateFunction>("CreateEngine");
	if (Create == nullptr)
	{
		ReportFatal("Engine assembly exports no CreateEngine");
	}

	IEngine* App = Create();
	if (!App)
	{
		ReportFatal("CreateEngine returned null");
	}

	// Bring the (anonymous) root engine up, run its main loop, then shut it
	// down symmetrically. The root is never known by concrete type — only by
	// the IEngine anchor.
	App->Initialize(Argc, Argv);

	const int Result = App->Main();

	App->Shutdown();
	delete App; // IEngine virtual dtor — removes the whole object through the DLL.
	return Result;
}

} // namespace Maho