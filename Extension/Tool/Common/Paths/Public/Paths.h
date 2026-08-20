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
	/** Identity tag — this is a Tool. */
	using FTags = TTypeList<FToolTag>;

	/** Resolve a virtual path "Alias/Sub/Path" (or "Alias:Sub/Path") to a physical path. */
	[[nodiscard]] std::filesystem::path Resolve(std::string_view VirtualPath) const;

	/** True when the alias is registered. */
	[[nodiscard]] bool HasRoot(std::string_view Alias) const;

protected:
	/** Register a root alias (e.g. "Engine" -> <engine dir>). Lifecycle write. */
	void SetRoot(std::string_view Alias, std::filesystem::path Path);

	/** Clear all registered root aliases. Lifecycle write. */
	void Clear();

private:
	template <typename TExtension, typename TStage>
	friend bool Maho::ExecuteExtension(TStage Stage);

	std::map<std::string, std::filesystem::path> Roots;
};

} // namespace Paths

} // namespace Maho
