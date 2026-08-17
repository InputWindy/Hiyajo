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
	const auto Create = ToolkitAssembly.GetProc<IExtension<EToolStage>*()>("CreateTool");
	if (Create == nullptr)
	{
		ToolkitAssembly.Unload();
		return false;
	}
	Toolkit = Create();
	if (Toolkit == nullptr)
	{
		ToolkitAssembly.Unload();
		return false;
	}
	Toolkit->ExecuteStage(EToolStage::Init);   // drive the pre-app init
	return true;
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
	const auto Create = EngineAssembly.GetProc<IExtension<EEngineStage>*()>("CreateEngine");
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
		if (Toolkit != nullptr)
		{
			Toolkit->ExecuteStage(EToolStage::Shutdown);   // drive the pre-app shutdown
		}
		delete Engine;                  // virtual dtor via IExtension
		Engine = nullptr;
		delete Toolkit;                 // virtual dtor via IExtension
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
