#pragma once

#include "PathsApi.h"
#include <Maho.h>
#include <Engine/PluginTemplates.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace Maho
{

namespace Paths
{

/** Paths plugin's own drive stage — the host passes it to Execute<Stage>(). */
enum class EPathsStage : std::uint8_t
{
	Init = 0,
	Shutdown,
};

/** Path resolution extension (project/engine roots). Driven by EPathsStage. */
class MAHO_PATHS_API FPaths : public TTool<FPaths>
{
public:
	/** Stage dispatch — called by `scheduler.Execute<EPathsStage, ...>()`. */
	[[nodiscard]] bool ExecuteStage(EPathsStage Stage);

	/** Register a root alias (e.g. "Engine" -> <engine dir>). */
	void SetRoot(std::string_view Alias, std::filesystem::path Path);

	/** Resolve a virtual path "Alias/Sub/Path" (or "Alias:Sub/Path") to a physical path. */
	[[nodiscard]] std::filesystem::path Resolve(std::string_view VirtualPath) const;

	/** True when the alias is registered. */
	[[nodiscard]] bool HasRoot(std::string_view Alias) const;

private:
	std::map<std::string, std::filesystem::path> Roots;
};

} // namespace Paths

} // namespace Maho
