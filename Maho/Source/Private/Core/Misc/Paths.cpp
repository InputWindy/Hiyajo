#include <Core/Misc/Paths.h>

#include <Core/Misc/Log.h>
#include <Core/Misc/Utf8Path.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#	endif
#	include <Windows.h>
#endif

#if !defined(MAHO_ENGINE_ROOT)
#	define MAHO_ENGINE_ROOT ""
#endif

namespace Maho
{

namespace fs = std::filesystem;

std::string FPaths::ProjectDir;
std::string FPaths::EngineDir;
std::string FPaths::ProjectContentDir;
std::string FPaths::EngineContentDir;
std::vector<FPathMount> FPaths::MountPoints;
bool FPaths::bInitialized = false;

namespace
{

constexpr const char* GPackageExtension = ".casset";

[[nodiscard]] bool LooksLikeProjectRoot(const fs::path& Candidate)
{
	std::error_code ErrorCode;
	if (!fs::is_directory(Candidate, ErrorCode) || ErrorCode)
	{
		return false;
	}
	if (fs::is_regular_file(Candidate / "Config" / "DefaultEngine.ini", ErrorCode) && !ErrorCode)
	{
		return true;
	}
	for (const fs::directory_entry& Entry : fs::directory_iterator(Candidate, ErrorCode))
	{
		if (ErrorCode)
		{
			break;
		}
		if (!Entry.is_regular_file(ErrorCode) || ErrorCode)
		{
			continue;
		}
		if (Entry.path().extension().string() == ".cproject")
		{
			return true;
		}
	}
	return false;
}

[[nodiscard]] bool LooksLikeEngineRoot(const fs::path& Candidate)
{
	std::error_code ErrorCode;
	return fs::is_regular_file(Candidate / "Maho" / "CMakeLists.txt", ErrorCode) && !ErrorCode;
}

[[nodiscard]] fs::path DetectProjectDir(const fs::path& ExeDir)
{
	std::error_code ErrorCode;
	const fs::path Cwd = fs::current_path(ErrorCode);
	if (!ErrorCode && LooksLikeProjectRoot(Cwd))
	{
		return fs::weakly_canonical(Cwd, ErrorCode);
	}

	if (LooksLikeProjectRoot(ExeDir))
	{
		return fs::weakly_canonical(ExeDir, ErrorCode);
	}

	fs::path Climb = ExeDir;
	for (int Step = 0; Step < 5; ++Step)
	{
		Climb = Climb.parent_path();
		if (Climb.empty())
		{
			break;
		}
		if (LooksLikeProjectRoot(Climb))
		{
			return fs::weakly_canonical(Climb, ErrorCode);
		}
	}

	// Climb failed — caller should use engine-root sibling scan.

	if (!ErrorCode)
	{
		return fs::weakly_canonical(Cwd, ErrorCode);
	}
	return Cwd;
}

[[nodiscard]] fs::path DetectEngineDir(const fs::path& ProjectRoot)
{
	std::error_code ErrorCode;

	const fs::path CompiledRoot(MAHO_ENGINE_ROOT);
	if (!CompiledRoot.empty() && LooksLikeEngineRoot(CompiledRoot))
	{
		return fs::weakly_canonical(CompiledRoot, ErrorCode);
	}

#if defined(_WIN32)
	{
		char EnvBuf[MAX_PATH] = {};
		const DWORD EnvLen = GetEnvironmentVariableA("MAHO_ENGINE_ROOT", EnvBuf, MAX_PATH);
		if (EnvLen > 0 && EnvLen < MAX_PATH)
		{
			const fs::path FromEnv(EnvBuf);
			if (LooksLikeEngineRoot(FromEnv))
			{
				return fs::weakly_canonical(FromEnv, ErrorCode);
			}
		}
	}
#else
	if (const char* EnvRoot = std::getenv("MAHO_ENGINE_ROOT"))
	{
		const fs::path FromEnv(EnvRoot);
		if (LooksLikeEngineRoot(FromEnv))
		{
			return fs::weakly_canonical(FromEnv, ErrorCode);
		}
	}
#endif

	const fs::path Sibling = ProjectRoot.parent_path() / "Maho";
	if (LooksLikeEngineRoot(Sibling))
	{
		return fs::weakly_canonical(Sibling, ErrorCode);
	}

	const fs::path ExeDir = FPaths::GetExecutableDir();
	if (LooksLikeEngineRoot(ExeDir))
	{
		return fs::weakly_canonical(ExeDir, ErrorCode);
	}

	// Packaged install: <ExeDir>/Engine/Content — EngineDir is the install root (ExeDir).
	const fs::path PackagedEngineContent = ExeDir / "Engine" / "Content";
	if (fs::is_directory(PackagedEngineContent, ErrorCode) && !ErrorCode)
	{
		return fs::weakly_canonical(ExeDir, ErrorCode);
	}

	// Do not fall back to ProjectRoot — that mounts engine content under the game tree.
	return {};
}

[[nodiscard]] std::string AbsolutizeUnder(const fs::path& Root, const std::string& RelOrAbs)
{
	if (RelOrAbs.empty())
	{
		return PathToUtf8(Root);
	}
	const fs::path AsPath = PathFromUtf8(RelOrAbs);
	std::error_code ErrorCode;
	if (AsPath.is_absolute())
	{
		return PathToUtf8(fs::weakly_canonical(AsPath, ErrorCode));
	}
	return PathToUtf8(fs::weakly_canonical(Root / AsPath, ErrorCode));
}

[[nodiscard]] std::string NormalizeVirtualRoot(std::string Root)
{
	for (char& Ch : Root)
	{
		if (Ch == '\\')
		{
			Ch = '/';
		}
	}
	while (!Root.empty() && Root.back() == '/')
	{
		Root.pop_back();
	}
	if (Root.empty() || Root[0] != '/')
	{
		Root.insert(Root.begin(), '/');
	}
	return Root;
}

[[nodiscard]] bool StartsWithIgnoreCase(const std::string& Text, const std::string& Prefix)
{
	if (Text.size() < Prefix.size())
	{
		return false;
	}
#if defined(_WIN32)
	for (std::size_t Index = 0; Index < Prefix.size(); ++Index)
	{
		char A = Text[Index];
		char B = Prefix[Index];
		if (A >= 'A' && A <= 'Z')
		{
			A = static_cast<char>(A - 'A' + 'a');
		}
		if (B >= 'A' && B <= 'Z')
		{
			B = static_cast<char>(B - 'A' + 'a');
		}
		if (A != B)
		{
			return false;
		}
	}
	return true;
#else
	return Text.compare(0, Prefix.size(), Prefix) == 0;
#endif
}

[[nodiscard]] std::string ToForwardSlashes(std::string Path)
{
	for (char& Ch : Path)
	{
		if (Ch == '\\')
		{
			Ch = '/';
		}
	}
	return Path;
}

[[nodiscard]] bool EndsWithPackageExtension(const std::string& Path)
{
	const std::string Ext = GPackageExtension;
	if (Path.size() < Ext.size())
	{
		return false;
	}
	return Path.compare(Path.size() - Ext.size(), Ext.size(), Ext) == 0;
}

[[nodiscard]] std::string StripPackageExtension(std::string Path)
{
	if (EndsWithPackageExtension(Path))
	{
		Path.resize(Path.size() - std::char_traits<char>::length(GPackageExtension));
	}
	return Path;
}

[[nodiscard]] fs::path DetectEngineContentDir(const fs::path& EngineRoot, const fs::path& ExeDir)
{
	std::error_code ErrorCode;
	const fs::path EngineMahoContent = EngineRoot / "Maho" / "Content";
	if (fs::is_directory(EngineMahoContent, ErrorCode) && !ErrorCode)
	{
		return fs::weakly_canonical(EngineMahoContent, ErrorCode);
	}

	const fs::path Packaged = ExeDir / "Engine" / "Content";
	if (fs::is_directory(Packaged, ErrorCode) && !ErrorCode)
	{
		return fs::weakly_canonical(Packaged, ErrorCode);
	}

	// Prefer engine-module Content even if not created yet (dev layout).
	if (!EngineRoot.empty())
	{
		return (EngineRoot / "Maho" / "Content").lexically_normal();
	}
	return (ExeDir / "Engine" / "Content").lexically_normal();
}

} // namespace

fs::path FPaths::GetExecutableDir()
{
	std::error_code ErrorCode;
#if defined(_WIN32)
	wchar_t ModulePathW[MAX_PATH] = {};
	const DWORD Length = GetModuleFileNameW(nullptr, ModulePathW, MAX_PATH);
	if (Length > 0 && Length < MAX_PATH)
	{
		return fs::path(ModulePathW).parent_path();
	}
#endif
	return fs::current_path(ErrorCode);
}

void FPaths::Initialize(FConfig& InOutConfig)
{
	const fs::path ExeDir = GetExecutableDir();
	const fs::path ProjectRoot = DetectProjectDir(ExeDir);
	const fs::path EngineRoot = DetectEngineDir(ProjectRoot);

	ProjectDir = PathToUtf8(ProjectRoot);
	EngineDir = PathToUtf8(EngineRoot);
	InOutConfig.ProjectDir = ProjectDir;
	InOutConfig.EngineDir = EngineDir;

	if (EngineRoot.empty())
	{
		MAHO_CORE_WARN(
			"FPaths::Initialize: EngineDir not detected — shaders/content fall back under executable");
	}

	InOutConfig.ProjectConfigDir = AbsolutizeUnder(ProjectRoot, InOutConfig.ProjectConfigDir.empty()
		? "Config"
		: InOutConfig.ProjectConfigDir);
	InOutConfig.ProjectScriptsDir = AbsolutizeUnder(ProjectRoot, InOutConfig.ProjectScriptsDir.empty()
		? "Scripts"
		: InOutConfig.ProjectScriptsDir);
	InOutConfig.ProjectContentDir = AbsolutizeUnder(ProjectRoot, InOutConfig.ProjectContentDir.empty()
		? "Content"
		: InOutConfig.ProjectContentDir);
	InOutConfig.SavedDir = AbsolutizeUnder(ProjectRoot, InOutConfig.SavedDir.empty()
		? "Saved"
		: InOutConfig.SavedDir);
	InOutConfig.CachedDir = AbsolutizeUnder(ProjectRoot, InOutConfig.CachedDir.empty()
		? "Cached"
		: InOutConfig.CachedDir);
	InOutConfig.ProjectShadersDir = AbsolutizeUnder(ProjectRoot, InOutConfig.ProjectShadersDir.empty()
		? "Shaders"
		: InOutConfig.ProjectShadersDir);
	InOutConfig.ProjectPluginsDir = AbsolutizeUnder(ProjectRoot, InOutConfig.ProjectPluginsDir.empty()
		? "Plugins"
		: InOutConfig.ProjectPluginsDir);

	const fs::path EngineMahoShaders = EngineRoot / "Maho" / "Shaders";
	std::error_code ErrorCode;
	if (InOutConfig.EngineShadersDir.empty()
		|| !PathFromUtf8(InOutConfig.EngineShadersDir).is_absolute())
	{
		if (fs::is_directory(EngineMahoShaders, ErrorCode) && !ErrorCode)
		{
			InOutConfig.EngineShadersDir = PathToUtf8(fs::weakly_canonical(EngineMahoShaders, ErrorCode));
		}
		else
		{
			InOutConfig.EngineShadersDir = AbsolutizeUnder(
				ExeDir,
				InOutConfig.EngineShadersDir.empty() ? "Engine/Shaders" : InOutConfig.EngineShadersDir);
		}
	}

	if (InOutConfig.EnginePluginsDir.empty()
		|| !PathFromUtf8(InOutConfig.EnginePluginsDir).is_absolute())
	{
		const fs::path EnginePlugins = EngineRoot / "Maho" / "Plugins";
		if (fs::is_directory(EnginePlugins, ErrorCode) && !ErrorCode)
		{
			InOutConfig.EnginePluginsDir = PathToUtf8(fs::weakly_canonical(EnginePlugins, ErrorCode));
		}
		else
		{
			InOutConfig.EnginePluginsDir = AbsolutizeUnder(
				ExeDir,
				InOutConfig.EnginePluginsDir.empty() ? "Engine/Plugins" : InOutConfig.EnginePluginsDir);
		}
	}

	ProjectContentDir = InOutConfig.ProjectContentDir;
	EngineContentDir = PathToUtf8(DetectEngineContentDir(EngineRoot, ExeDir));

	MountPoints.clear();
	RegisterMountPoint("/Game", ProjectContentDir);
	RegisterMountPoint("/Engine", EngineContentDir);

	bInitialized = true;
}

bool FPaths::IsInitialized()
{
	return bInitialized;
}

const std::string& FPaths::GetProjectDir()
{
	return ProjectDir;
}

const std::string& FPaths::GetEngineDir()
{
	return EngineDir;
}

const std::string& FPaths::GetProjectContentDir()
{
	return ProjectContentDir;
}

const std::string& FPaths::GetEngineContentDir()
{
	return EngineContentDir;
}

std::string FPaths::Combine(const std::string& A, const std::string& B)
{
	if (A.empty())
	{
		return B;
	}
	if (B.empty())
	{
		return A;
	}
	return PathToUtf8(PathFromUtf8(A) / PathFromUtf8(B));
}

std::string FPaths::Combine(const std::string& A, const std::string& B, const std::string& C)
{
	return Combine(Combine(A, B), C);
}

std::string FPaths::MakeAbsolute(const std::string& Path)
{
	if (Path.empty())
	{
		return {};
	}
	std::error_code ErrorCode;
	const fs::path AsPath = PathFromUtf8(Path);
	return PathToUtf8(fs::weakly_canonical(fs::absolute(AsPath, ErrorCode), ErrorCode));
}

std::string FPaths::MakeProjectRelativeAbsolute(const std::string& RelOrAbs)
{
	if (RelOrAbs.empty())
	{
		return ProjectDir;
	}
	const fs::path AsPath = PathFromUtf8(RelOrAbs);
	if (AsPath.is_absolute())
	{
		return MakeAbsolute(RelOrAbs);
	}
	return Combine(ProjectDir, RelOrAbs);
}

void FPaths::RegisterMountPoint(std::string VirtualRoot, std::string DiskRoot)
{
	VirtualRoot = NormalizeVirtualRoot(std::move(VirtualRoot));
	if (VirtualRoot.empty() || VirtualRoot == "/")
	{
		return;
	}

	std::error_code ErrorCode;
	fs::path DiskPath = PathFromUtf8(DiskRoot);
	if (!DiskPath.empty())
	{
		DiskRoot = PathToUtf8(fs::weakly_canonical(DiskPath, ErrorCode));
		if (ErrorCode || DiskRoot.empty())
		{
			DiskRoot = PathToUtf8(DiskPath.lexically_normal());
		}
	}

	for (FPathMount& Mount : MountPoints)
	{
		if (Mount.VirtualRoot == VirtualRoot)
		{
			Mount.DiskRoot = std::move(DiskRoot);
			return;
		}
	}
	MountPoints.push_back(FPathMount{std::move(VirtualRoot), std::move(DiskRoot)});
}

void FPaths::UnregisterMountPoint(const std::string& VirtualRoot)
{
	const std::string Normalized = NormalizeVirtualRoot(VirtualRoot);
	MountPoints.erase(
		std::remove_if(
			MountPoints.begin(),
			MountPoints.end(),
			[&](const FPathMount& Mount)
			{
				return Mount.VirtualRoot == Normalized;
			}),
		MountPoints.end());
}

const std::vector<FPathMount>& FPaths::GetMountPoints()
{
	return MountPoints;
}

void FPaths::EnsureMountDirectories()
{
	std::error_code ErrorCode;
	for (const FPathMount& Mount : MountPoints)
	{
		if (!Mount.DiskRoot.empty())
		{
			fs::create_directories(Mount.DiskRoot, ErrorCode);
		}
	}
}

const char* FPaths::GetPackageExtension()
{
	return GPackageExtension;
}

bool FPaths::IsPackagePath(const std::string& Path)
{
	const std::string Normalized = NormalizePackagePath(Path);
	if (Normalized.size() < 2 || Normalized[0] != '/')
	{
		return false;
	}
	// Windows drive or UNC must not look like a package path.
	if (Normalized.size() >= 3 && Normalized[2] == ':' )
	{
		return false;
	}
	for (const FPathMount& Mount : MountPoints)
	{
		if (Normalized == Mount.VirtualRoot)
		{
			return true;
		}
		const std::string Prefix = Mount.VirtualRoot + "/";
		if (Normalized.rfind(Prefix, 0) == 0)
		{
			return true;
		}
	}
	// Unregistered but still a long-path-shaped string (/Something/...).
	return Normalized.find('/', 1) != std::string::npos || Normalized.size() > 1;
}

std::string FPaths::NormalizePackagePath(std::string Path)
{
	Path = ToForwardSlashes(std::move(Path));
	if (Path.empty())
	{
		return Path;
	}

	std::string Out;
	Out.reserve(Path.size());
	bool bLastWasSlash = false;
	for (const char Ch : Path)
	{
		if (Ch == '/')
		{
			if (!bLastWasSlash)
			{
				Out.push_back('/');
				bLastWasSlash = true;
			}
			continue;
		}
		Out.push_back(Ch);
		bLastWasSlash = false;
	}
	while (Out.size() > 1 && Out.back() == '/')
	{
		Out.pop_back();
	}
	return Out;
}

std::string FPaths::GetPackageName(const std::string& ObjectOrPackagePath)
{
	std::string Path = NormalizePackagePath(ObjectOrPackagePath);
	const std::size_t LastSlash = Path.find_last_of('/');
	const std::size_t LastDot = Path.find_last_of('.');
	// Soft object path: "/Game/Foo.Bar" — peel ".Bar" when the dot is after the last '/'.
	if (LastDot != std::string::npos
		&& (LastSlash == std::string::npos || LastDot > LastSlash)
		&& LastDot + 1 < Path.size()
		&& Path.find('/', LastDot) == std::string::npos)
	{
		// Keep multi-dot package filenames like "Demo.pkg" out — only peel when not a known file ext.
		const std::string AfterDot = Path.substr(LastDot);
		if (AfterDot != GPackageExtension && AfterDot != ".json" && AfterDot != ".uasset" && AfterDot != ".umap")
		{
			Path.resize(LastDot);
		}
	}
	return Path;
}

std::string FPaths::ConvertVirtualPathToFilename(const std::string& VirtualPath)
{
	const std::string Normalized = NormalizePackagePath(VirtualPath);
	if (Normalized.empty())
	{
		return {};
	}

	const FPathMount* Best = nullptr;
	for (const FPathMount& Mount : MountPoints)
	{
		if (Normalized == Mount.VirtualRoot)
		{
			Best = &Mount;
			break;
		}
		const std::string Prefix = Mount.VirtualRoot + "/";
		if (Normalized.rfind(Prefix, 0) == 0)
		{
			if (!Best || Mount.VirtualRoot.size() > Best->VirtualRoot.size())
			{
				Best = &Mount;
			}
		}
	}
	if (!Best)
	{
		return {};
	}

	if (Normalized == Best->VirtualRoot)
	{
		return Best->DiskRoot;
	}

	const std::string Relative = Normalized.substr(Best->VirtualRoot.size() + 1);
	return Combine(Best->DiskRoot, Relative);
}

std::string FPaths::ConvertFilenameToVirtualPath(const std::string& Filename)
{
	if (Filename.empty())
	{
		return {};
	}

	std::error_code ErrorCode;
	const fs::path AsPath = PathFromUtf8(Filename);
	std::string Absolute = PathToUtf8(fs::weakly_canonical(AsPath, ErrorCode));
	if (ErrorCode || Absolute.empty())
	{
		Absolute = PathToUtf8(AsPath.lexically_normal());
	}
	Absolute = ToForwardSlashes(std::move(Absolute));

	const FPathMount* Best = nullptr;
	std::string BestDiskForward;
	for (const FPathMount& Mount : MountPoints)
	{
		std::string DiskForward = ToForwardSlashes(Mount.DiskRoot);
		while (!DiskForward.empty() && DiskForward.back() == '/')
		{
			DiskForward.pop_back();
		}
		if (!StartsWithIgnoreCase(Absolute, DiskForward))
		{
			continue;
		}
		if (Absolute.size() > DiskForward.size()
			&& Absolute[DiskForward.size()] != '/')
		{
			continue;
		}
		if (!Best || DiskForward.size() > BestDiskForward.size())
		{
			Best = &Mount;
			BestDiskForward = std::move(DiskForward);
		}
	}
	if (!Best)
	{
		return {};
	}

	if (Absolute.size() == BestDiskForward.size())
	{
		return Best->VirtualRoot;
	}
	const std::string Relative = Absolute.substr(BestDiskForward.size() + 1);
	return NormalizePackagePath(Best->VirtualRoot + "/" + Relative);
}

std::string FPaths::ConvertPackageNameToFilename(const std::string& PackageOrObjectPath)
{
	const std::string PackageName = GetPackageName(PackageOrObjectPath);
	std::string Disk = ConvertVirtualPathToFilename(PackageName);
	if (Disk.empty())
	{
		return {};
	}
	if (!EndsWithPackageExtension(Disk))
	{
		Disk += GPackageExtension;
	}
	return Disk;
}

std::string FPaths::ConvertFilenameToPackageName(const std::string& Filename)
{
	std::string Virtual = ConvertFilenameToVirtualPath(Filename);
	if (Virtual.empty())
	{
		return {};
	}
	Virtual = StripPackageExtension(std::move(Virtual));
	return NormalizePackagePath(std::move(Virtual));
}

} // namespace Maho
