#pragma once

#include "PathsApi.h"
#include <Engine.h>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace Maho
{

namespace Paths
{

/** Path resolution extension (project/engine roots). Pre-app toolkit (driven by EToolStage). */
class MAHO_PATHS_API FPaths : public TExtension<EToolStage, FPaths>
{
public:
	[[nodiscard]] bool ExecuteStage(EToolStage Stage) override;

	/** Register a root alias (e.g. "Engine" -> <engine dir>). */
	void SetRoot(std::string_view Alias, std::filesystem::path Path);

	/** Resolve a virtual path "Alias/Sub/Path" (or "Alias:Sub/Path") to a physical path. */
	[[nodiscard]] std::filesystem::path Resolve(std::string_view VirtualPath) const;

	/** True when the alias is registered. */
	[[nodiscard]] bool HasRoot(std::string_view Alias) const;

protected:
	friend TSingleton<FPaths>;
	FPaths() = default;

	std::map<std::string, std::filesystem::path> Roots;
};

} // namespace Paths

} // namespace Maho
