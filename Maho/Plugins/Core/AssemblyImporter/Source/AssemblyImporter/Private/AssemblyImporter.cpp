#include <AssemblyImporter.h>

namespace Maho::AssemblyImporter
{

bool FAssemblyImporter::ImportToolkit(std::string_view Path)
{
	if (Toolkit != nullptr)
	{
		return false;   // already imported
	}
	if (!ToolkitAssembly.Load(Path))
	{
		return false;
	}
	const auto Create = ToolkitAssembly.GetProc<FToolkitBase*()>("CreateTool");
	if (Create == nullptr)
	{
		ToolkitAssembly.Unload();
		return false;
	}
	Toolkit = Create();   // ctor runs Init
	return Toolkit != nullptr;
}

bool FAssemblyImporter::ImportEngine(std::string_view Path)
{
	if (Engine != nullptr)
	{
		return false;   // already imported
	}
	if (!EngineAssembly.Load(Path))
	{
		return false;
	}
	const auto Create = EngineAssembly.GetProc<IRunable*()>("CreateEngine");
	if (Create == nullptr)
	{
		EngineAssembly.Unload();
		return false;
	}
	Engine = Create();
	return Engine != nullptr;
}

bool FAssemblyImporter::ExecuteStage(EEngineStage Stage)
{
	if (Stage == EEngineStage::Shutdown)
	{
		delete Engine;                  // dtor runs the engine's shutdown
		Engine = nullptr;
		delete Toolkit;                 // dtor runs the toolkit's shutdown
		Toolkit = nullptr;
		EngineAssembly.Unload();
		ToolkitAssembly.Unload();
	}
	return true;
}

} // namespace Maho::AssemblyImporter

// ── Dynamic entry — the thin launcher loads this DLL directly. ──

extern "C" MAHO_ASSEMBLYIMPORTER_API Maho::AssemblyImporter::FAssemblyImporter* CreateAssemblyImporter()
{
	return &Maho::AssemblyImporter::FAssemblyImporter::Get();
}
