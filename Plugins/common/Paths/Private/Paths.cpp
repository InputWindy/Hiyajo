#include "Paths.h"

#include <Engine/PluginCatalog.h>

#include <system_error>
#include <utility>

namespace Maho::Paths
{

static FPaths* GPaths = nullptr;

namespace
{

/** Virtual root aliases (UE-style): /Game -> project Content/, /Engine -> engine Content/. */
constexpr std::string_view GameAlias = "Game";
constexpr std::string_view EngineAlias = "Engine";

bool IsDirectory(const std::filesystem::path& P)
{
	if (P.empty())
	{
		return false;
	}
	std::error_code Ec;
	return std::filesystem::is_directory(P, Ec) && !Ec;
}

/** Project root. The exe lives at <ProjectRoot>/Intermediate/Binaries/<Config>/, so
 *  three levels up from the exe dir is the project root; a binary placed elsewhere
 *  falls back to the working directory. */
std::filesystem::path ProjectRoot()
{
	const std::filesystem::path ExeDir = FPluginCatalog::ExecutableDir();
	std::error_code Ec;
	if (!ExeDir.empty())
	{
		const std::filesystem::path Up = ExeDir / ".." / ".." / "..";
		if (IsDirectory(Up))
		{
			return std::filesystem::weakly_canonical(Up, Ec);
		}
	}
	return std::filesystem::current_path(Ec);
}

/** Filesystem path from a UTF-8 string. A narrow std::filesystem::path decodes with the
 *  ANSI code page, so a manifest path holding non-ASCII characters would come out
 *  mangled and the root would look missing; the UTF-8 overload decodes it properly. */
std::filesystem::path Utf8Path(std::string_view Utf8)
{
	return std::filesystem::path(
		std::u8string(reinterpret_cast<const char8_t*>(Utf8.data()), Utf8.size()));
}

/** Engine source root from the generated manifest (empty when unavailable). */
std::filesystem::path EngineRoot()
{
	FPluginCatalog& Catalog = FPluginCatalog::Get();
	if (!Catalog.IsLoaded())
	{
		Catalog.Load();
	}
	return Utf8Path(Catalog.GetEngineRoot());
}

} // namespace

FPaths* GetPaths()
{
	return GPaths;
}

void FPaths::Initialize(FEngineBase&)
{
	GPaths = this;

	// Defaults only fill aliases nobody registered -- repeated Initialize keeps an
	// explicit SetRoot intact. Roots are registered unconditionally: a missing
	// directory is a valid (empty) content root, not an error.
	const std::filesystem::path ExeDir = FPluginCatalog::ExecutableDir();
	const std::filesystem::path DeployedGame = ExeDir / "Content" / "Game";
	const std::filesystem::path DeployedEngine = ExeDir / "Content" / "Engine";

	// A packaged build carries its content next to the binary; a dev build reads the
	// source trees. Deployed wins when present.
	const std::filesystem::path Game = IsDirectory(DeployedGame) ? DeployedGame : (ProjectRoot() / "Content");
	const std::filesystem::path Engine = IsDirectory(DeployedEngine) ? DeployedEngine : (EngineRoot() / "Content");

	if (!HasRoot(GameAlias))
	{
		Roots[std::string(GameAlias)] = Game;
	}
	if (!HasRoot(EngineAlias))
	{
		Roots[std::string(EngineAlias)] = Engine;
	}
}

void FPaths::Shutdown(FEngineBase&)
{
	GPaths = nullptr;
	Roots.clear();
}

void FPaths::SetRoot(std::string_view Alias, std::filesystem::path Path)
{
	Roots[std::string(Alias)] = std::move(Path);
}

std::filesystem::path FPaths::Resolve(std::string_view VirtualPath) const
{
	// UE-style leading slash: "/Game/X" is the display form of "Game/X". The original
	// view is kept for the pass-through returns -- an unmatched path must come back
	// verbatim (that is how an absolute physical path stays absolute).
	const std::string_view Original = VirtualPath;
	if (const std::size_t First = VirtualPath.find_first_not_of('/'); First != std::string_view::npos)
	{
		VirtualPath.remove_prefix(First);
	}

	const std::size_t Separator = VirtualPath.find_first_of("/:");
	if (Separator == std::string_view::npos)
	{
		const auto It = Roots.find(std::string(VirtualPath));
		if (It != Roots.end())
		{
			return It->second;
		}
		return std::filesystem::path(std::string(Original));
	}

	const std::string_view Alias = VirtualPath.substr(0, Separator);
	const auto It = Roots.find(std::string(Alias));
	if (It == Roots.end())
	{
		return std::filesystem::path(std::string(Original));
	}

	std::filesystem::path Result = It->second;
	Result /= std::string(VirtualPath.substr(Separator + 1));
	return Result;
}

bool FPaths::HasRoot(std::string_view Alias) const
{
	return Roots.find(std::string(Alias)) != Roots.end();
}

} // namespace Maho::Paths

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_PATHS_API Maho::FLayerBase* CreateLayer()
{
	return Maho::Paths::FPaths::CreateLayer();
}

