#pragma once

#include "PathsApi.h"
#include <Maho.h>
#include <Engine/Tool.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace Maho
{

namespace Paths
{

/** Path resolution extension (project/engine roots). */
class MAHO_PATHS_API FPathsTool : public TTool<FPathsTool>
{
public:
	/** Aggregate identity tags: base + this Tool's own (empty for now). */
	using FTags = TCatch<typename Maho::TTool<FPathsTool>::FTags, TTypeList<>>::Type;

	/** Resolve a virtual path "Alias/Sub/Path" (or "Alias:Sub/Path") to a physical path. */
	[[nodiscard]] std::filesystem::path Resolve(std::string_view VirtualPath) const;

	/** True when the alias is registered. */
	[[nodiscard]] bool HasRoot(std::string_view Alias) const;

	/** Register a root alias (e.g. "Engine" -> <engine dir>). */
	void SetRoot(std::string_view Alias, std::filesystem::path Path);

	/** Clear all registered root aliases. */
	void Clear();

private:
	std::map<std::string, std::filesystem::path> Roots;
};

} // namespace Paths

} // namespace Maho
