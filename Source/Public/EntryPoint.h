#pragma once

#include <Core/Fatal.h>
#include <Engine/Layer.h>

namespace Maho
{

/**
 * Unified app driver — install an extension DLL and execute its root instance.
 *
 * No engine/tool preset: the extension is a self-contained DLL exporting
 * `CreateLayer()` → a FLayerBase*. The entry point loads it via FAssembly,
 * brings the anonymous root up (Initialize → subtree), forwards to its main
 * capability, then takes it down symmetrically (Shutdown + dtor teardown).
 *
 *   main()/WinMain() → Maho::Main(Argc, Argv)
 *     InstallFatalHandlers()
 *     FAssembly Load(argv[1])          // install
 *     CreateLayer() → FLayerBase*      // create the root instance (anonymous)
 *     dynamic_cast<IInit*> → Initialize(Argc, Argv)   // bring the subtree up
 *     dynamic_cast<IMain*>            // does it own a run entry?
 *       → Main()                       // execute
 *     dynamic_cast<IShutdown*> → Shutdown()   // symmetric teardown
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

	// Create the root instance — the exported CreateLayer factory.
	using CreateFunction = FLayerBase* (*)();
	auto Create = Extension.GetProcAs<CreateFunction>("CreateLayer");
	if (Create == nullptr)
	{
		ReportFatal("Extension assembly exports no CreateLayer");
	}

	FLayerBase* App = Create();
	if (!App)
	{
		ReportFatal("CreateLayer returned null");
	}

	// Bring the (anonymous) root layer up: initialize its subtree, run its main
	// loop, then shut it down symmetrically. The root is never known by concrete
	// type — only by the FLayerBase anchor + capability interfaces.
	auto* InitCaps = dynamic_cast<IInit*>(App);
	if (!InitCaps)
	{
		ReportFatal("Extension root exposes no IInit");
	}
	InitCaps->Initialize(Argc, Argv); // 拉起：根初始化子树（项目根 override）

	auto* MainCaps = dynamic_cast<IMain*>(App);
	if (!MainCaps)
	{
		ReportFatal("Extension root exposes no IMain");
	}

	const int Result = MainCaps->Main();

	if (auto* ShutdownCaps = dynamic_cast<IShutdown*>(App))
	{
		ShutdownCaps->Shutdown(); // 收起：对称关闭（子树清理走 ~FLayer 析构递归）
	}
	delete App; // FLayerBase virtual dtor — removes the whole object through the DLL.
	return Result;
}

} // namespace Maho
