#pragma once

#include <Core/Fatal.h>
#include <Engine/Layer.h>

namespace Maho
{

/**
 * Unified app driver — install an extension DLL and execute its root instance.
 *
 * No engine/tool preset: the extension is a self-contained DLL exporting
 * `CreateExtension()` → a FLayerBase*. The entry point loads it via FAssembly,
 * brings the anonymous root up (OnInstall → subtree), forwards to its main
 * capability, then takes it down symmetrically (OnUninstall + dtor teardown).
 *
 *   main()/WinMain() → Maho::Main(Argc, Argv)
 *     InstallFatalHandlers()
 *     FAssembly Load(argv[1])          // install
 *     CreateExtension() → FLayerBase* // create the root instance (anonymous)
 *     App->OnInstall()                // bring the subtree up
 *     dynamic_cast<IMain*>            // does it own a run entry?
 *       → Main()                       // execute
 *     App->OnUninstall()              // symmetric teardown
 *     delete App
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

	// Bring the (anonymous) root layer up: install its subtree, run its main
	// loop, then uninstall symmetrically. The root is never known by concrete
	// type — only by the FLayerBase anchor + the IMain capability.
	App->OnInstall(); // 拉起：根安装子树（项目根 override）

	auto* MainCaps = dynamic_cast<IMain*>(App);
	if (!MainCaps)
	{
		ReportFatal("Extension root exposes no IMain");
	}

	const int Result = MainCaps->Main();

	App->OnUninstall(); // 收起：对称卸载（子树清理走 ~FLayer 析构递归）
	delete App; // FLayerBase virtual dtor — removes the whole object through the DLL.
	return Result;
}

} // namespace Maho
