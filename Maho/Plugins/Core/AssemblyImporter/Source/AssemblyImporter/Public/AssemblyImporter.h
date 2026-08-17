#pragma once

#include "AssemblyImporterApi.h"
#include <Core/Assembly.h>
#include <Engine.h>

#include <string_view>

namespace Maho
{

namespace AssemblyImporter
{

/**
 * Imports the project's two aggregate assemblies — the toolkit (pre-app) and
 * the engine (main loop). Loads each DLL, creates the instance via its typed
 * factory (CreateTool → FToolkitBase*, CreateEngine → IRunable*), and destroys
 * both on Shutdown. The OS resolves each aggregate's transitive DLL imports
 * through its static link table — no recursive import here.
 */
class MAHO_ASSEMBLYIMPORTER_API FAssemblyImporter final
	: public TExtension<EEngineStage, FAssemblyImporter>
{
public:
	[[nodiscard]] bool ExecuteStage(EEngineStage Stage) override;

	/** Import the toolkit aggregate (exports CreateTool → FToolkitBase*). */
	bool ImportToolkit(std::string_view Path);

	/** Import the engine aggregate (exports CreateEngine → IRunable*). */
	bool ImportEngine(std::string_view Path);

	[[nodiscard]] FToolkitBase* GetToolkit() const { return Toolkit; }
	[[nodiscard]] IRunable* GetEngine() const { return Engine; }

private:
	friend TSingleton<FAssemblyImporter>;
	FAssemblyImporter() = default;

	FAssembly ToolkitAssembly;
	FAssembly EngineAssembly;
	FToolkitBase* Toolkit = nullptr;
	IRunable* Engine = nullptr;
};

} // namespace AssemblyImporter

} // namespace Maho
