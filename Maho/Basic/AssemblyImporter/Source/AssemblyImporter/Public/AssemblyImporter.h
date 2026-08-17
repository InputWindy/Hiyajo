#pragma once

#include "AssemblyImporterApi.h"
#include <Core/Core.h>
#include <Engine.h>
#include <Toolkit.h>

#include <string_view>

namespace Maho
{

namespace AssemblyImporter
{

/**
 * Imports the project's two aggregate assemblies — the toolkit (pre-app) and
 * the engine (main loop). Loads each DLL, creates the instance via its typed
 * factory, and drives it through the uniform IExtension<TStage> interface.
 * The OS resolves each aggregate's transitive DLL imports through its static
 * link table — no recursive import here.
 */
class MAHO_ASSEMBLYIMPORTER_API FAssemblyImporter final
	: public TExtension<EEngineStage, FAssemblyImporter>
{
public:
	[[nodiscard]] bool ExecuteStage(EEngineStage Stage) override;

	/** Import the toolkit aggregate (exports CreateTool → IExtension<EToolStage>*). */
	bool ImportToolkit(std::string_view Path);

	/** Import the engine aggregate (exports CreateEngine → IExtension<EEngineStage>*). */
	bool ImportEngine(std::string_view Path);

	[[nodiscard]] IExtension<EToolStage>* GetToolkit() const { return Toolkit; }
	[[nodiscard]] IExtension<EEngineStage>* GetEngine() const { return Engine; }

private:
	friend TSingleton<FAssemblyImporter>;
	FAssemblyImporter() = default;

	FAssembly ToolkitAssembly;
	FAssembly EngineAssembly;
	IExtension<EToolStage>* Toolkit = nullptr;
	IExtension<EEngineStage>* Engine = nullptr;
};

} // namespace AssemblyImporter

} // namespace Maho
