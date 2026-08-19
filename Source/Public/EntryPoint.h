#pragma once

#include <Maho.h>

namespace Maho
{

/**
 * Unified app driver — install an extension assembly and execute it.
 *
 * No engine/tool preset: the extension is a self-contained DLL
 * exporting `CreateExtension()` → an owning IAssembly*. The entry point
 * installs it via FAssembly, then forwards to IAssembly::Main.
 *
 *   main()/WinMain() → Maho::Main(Argc, Argv)
 *     InstallFatalHandlers()
 *     FAssembly Load(argv[1])       // install
 *     CreateExtension() → IAssembly* // create the assembly instance
 *     IAssembly->Main(Argc, Argv)    // execute
 */
inline int Main(int Argc, char** Argv)
{
	InstallFatalHandlers();

#ifndef MAHO_EXTENSION_NAME
#	define MAHO_EXTENSION_NAME "Extension.dll"
#endif
	// The extension path — first command-line argument, else the project's DLL.
	const char* ExtensionPath = (Argc > 1 && Argv[1] != nullptr) ? Argv[1] : MAHO_EXTENSION_NAME;

	// Install (load) the extension.
	FAssembly Extension(ExtensionPath);
	if (!Extension.IsLoaded())
	{
		ReportFatal("Failed to install extension assembly");
	}

	// Create the assembly instance.
	auto Create = Extension.GetProc<IAssembly*()>("CreateExtension");
	if (!Create)
	{
		ReportFatal("Extension assembly exports no CreateExtension");
	}

	IAssembly* App = Create();
	if (!App)
	{
		ReportFatal("CreateExtension returned null");
	}

	// Execute.
	const int Result = App->Main(Argc, Argv);
	delete App;
	return Result;
}

} // namespace Maho
