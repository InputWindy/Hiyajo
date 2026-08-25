#pragma once

#include <Core/Singleton.h>
#include <Engine/Layer.h>

#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace Maho
{
namespace Paths
{

/** Path resolution singleton — engine/project root aliases → physical paths. */
class FPaths
	: public TSingleton<FPaths>
	, public IPlugin<IInitialize, IShutdown>
{
public:
	/** Process-unique accessor — declared here, defined in Paths.cpp (in Paths.dll). */
	static FPaths& Get();

	void Initiate(int, char**) override { Roots.clear(); }
	void Shutdown() override { Roots.clear(); }

	/** Register a root alias (e.g. "Engine" → <engine dir>). */
	void SetRoot(std::string_view Alias, std::filesystem::path Path);

	/** Resolve "Alias/Sub/Path" (or "Alias:Sub/Path") to a physical path. */
	[[nodiscard]] std::filesystem::path Resolve(std::string_view VirtualPath) const;

	/** True when the alias is registered. */
	[[nodiscard]] bool HasRoot(std::string_view Alias) const;

private:
	std::map<std::string, std::filesystem::path> Roots;
};

} // namespace Paths
} // namespace Maho
