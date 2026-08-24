#pragma once

#include <Engine/Layer.h>

namespace Maho
{

/**
 * Unified app driver — install an extension DLL and execute its root instance.
 *
 * No engine/tool preset: the extension is a self-contained DLL exporting
 * `CreateExtension()` → a FLayerBase*. The entry point loads it via FAssembly,
 * looks up the root, then forwards to its main capability if it has one.
 *
 *   main()/WinMain() → Maho::Main(Argc, Argv)
 *     InstallFatalHandlers()
 *     FAssembly Load(argv[1])           // install
 *     CreateExtension() → FLayerBase* // create the root instance
 *     dynamic_cast<IMain*>              // does it own a run entry?
 *       → Main()                         // execute
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

	// Create the root instance.
	using CreateFunction = FLayerBase* (*)();
	auto Create = Extension.GetProc<CreateFunction>("CreateExtension");
	if (!Create)
	{
		ReportFatal("Extension assembly exports no CreateExtension");
	}

	FLayerBase* App = Create();
	if (!App)
	{
		ReportFatal("CreateExtension returned null");
	}

	// Execute through the main capability, if the root owns one.
	auto* MainCaps = dynamic_cast<IMain*>(App);
	if (!MainCaps)
	{
		ReportFatal("Extension root exposes no IMain");
	}

	const int Result = MainCaps->Main();
	delete App; // FLayerBase virtual dtor — removes the whole object through the DLL.
	return Result;
}

} // namespace Maho
