#pragma once

#include "PathsApi.h"
#include <Maho.h>
#include <Engine/Frame.h>

#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace Maho
{
namespace Paths
{

/** Path resolution engine layer - engine/project root aliases -> physical paths. */
class FPaths
	: public FFrameExtension, public IPipeline<IPreInit, IInit, IPostInit, IPreShutdown, IShutdown, IPostShutdown>
{
	MAHO_DECLARE_FRAME(FPaths);

private:
	// -- engine layer stages (scheduler-only) --
	void PreInitialize(FEngineBase&, FEngineContext&) override {}
	void Initialize(FEngineBase&, FEngineContext&) override;
	void PostInitialize(FEngineBase&, FEngineContext&) override {}
	void PreShutdown(FEngineBase&, FEngineContext&) override {}
	void Shutdown(FEngineBase&, FEngineContext&) override;
	void PostShutdown(FEngineBase&, FEngineContext&) override {}

public:
	/** Register a root alias (e.g. "Engine" - <engine dir>). */
	void SetRoot(std::string_view Alias, std::filesystem::path Path);

	/** Resolve "Alias/Sub/Path" (or "Alias:Sub/Path") to a physical path. */
	[[nodiscard]] std::filesystem::path Resolve(std::string_view VirtualPath) const;

	/** True when the alias is registered. */
	[[nodiscard]] bool HasRoot(std::string_view Alias) const;

private:
	std::map<std::string, std::filesystem::path> Roots;
};

/** Global accessor - set by Initialize, cleared by Shutdown. */
MAHO_PATHS_API FPaths* GetPaths();

} // namespace Paths
} // namespace Maho
