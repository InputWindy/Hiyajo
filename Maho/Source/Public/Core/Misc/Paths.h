#pragma once

#include <Core/Misc/Export.h>
#include <Core/Engine/Engine.h>

#include <filesystem>
#include <string>
#include <vector>

namespace Maho
{

/**
 * One virtual ↔ disk mount (UE FPackageName mount-point lite).
 * VirtualRoot always begins with '/' and has no trailing slash (e.g. "/Game").
 * DiskRoot is an absolute Content directory on disk.
 */
struct FPathMount
{
	std::string VirtualRoot;
	std::string DiskRoot;
};

/**
 * OS roots + UE-style package virtual filesystem.
 *
 * Package / virtual refs (no class / subobject yet):
 *   /Game/Maps/Demo                  → Project Content/Maps/Demo.casset
 *   /Engine/EngineMaterials/Default  → Engine Content/.../Default.casset
 * Soft object path (object identity): see FSoftObjectPath
 *   /Game/Maps/Demo.Demo             → same disk file as /Game/Maps/Demo
 *   UResource'/Game/Maps/Demo.Demo'  → class-qualified soft ref (parse-only)
 *
 * Call Initialize after FEngineBase::Configure. Default mounts: /Game, /Engine.
 */
class MAHO_API FPaths
{
public:
	FPaths() = delete;

	/**
	 * Detect ProjectDir / EngineDir, absolutize config dirs, register default mounts.
	 * Safe to call more than once (recomputes mounts from the new roots).
	 */
	static void Initialize(FConfig& InOutConfig);

	[[nodiscard]] static bool IsInitialized();
	[[nodiscard]] static const std::string& GetProjectDir();
	[[nodiscard]] static const std::string& GetEngineDir();
	[[nodiscard]] static const std::string& GetProjectContentDir();
	[[nodiscard]] static const std::string& GetEngineContentDir();

	/** Join root + relative segments with preferred separators; empty parts skipped. */
	[[nodiscard]] static std::string Combine(const std::string& A, const std::string& B);
	[[nodiscard]] static std::string Combine(
		const std::string& A,
		const std::string& B,
		const std::string& C);

	/** Absolute normalized path (lexically). Empty input → empty. */
	[[nodiscard]] static std::string MakeAbsolute(const std::string& Path);

	/** If RelOrAbs is absolute, return it; else ProjectDir / RelOrAbs. */
	[[nodiscard]] static std::string MakeProjectRelativeAbsolute(const std::string& RelOrAbs);

	/** Executable directory (…/Binaries/Win64/Debug), or cwd fallback. */
	[[nodiscard]] static std::filesystem::path GetExecutableDir();

	// ---------------------------------------------------------------------------
	// Mount registry (UE RegisterMountPoint lite)
	// ---------------------------------------------------------------------------

	/**
	 * Map VirtualRoot ("/Game") onto absolute DiskRoot. Replaces an existing root.
	 * VirtualRoot is normalized to leading '/' and no trailing '/'.
	 */
	static void RegisterMountPoint(std::string VirtualRoot, std::string DiskRoot);

	/** Remove a mount; no-op if missing. */
	static void UnregisterMountPoint(const std::string& VirtualRoot);

	[[nodiscard]] static const std::vector<FPathMount>& GetMountPoints();

	/** Create mount disk directories if missing (/Game and /Engine Content roots). */
	static void EnsureMountDirectories();

	// ---------------------------------------------------------------------------
	// Package / virtual path protocol
	// ---------------------------------------------------------------------------

	/** On-disk package suffix (Maho: ".casset"; self-contained engine asset). */
	[[nodiscard]] static const char* GetPackageExtension();

	/**
	 * True if Path looks like a long package / virtual path under a known mount
	 * (or any absolute path starting with '/' that is not a Windows drive).
	 */
	[[nodiscard]] static bool IsPackagePath(const std::string& Path);

	/** Normalize separators to '/', strip "./", collapse duplicate '/', drop trailing '/'. */
	[[nodiscard]] static std::string NormalizePackagePath(std::string Path);

	/**
	 * Soft object path → long package name: "/Game/Maps/Demo.Demo" → "/Game/Maps/Demo".
	 * Already a package path → returned normalized.
	 */
	[[nodiscard]] static std::string GetPackageName(const std::string& ObjectOrPackagePath);

	/**
	 * Virtual folder or package path → absolute disk path (no forced extension).
	 * "/Game/Maps" → "<ProjectContent>/Maps"
	 * Empty / unmatched → empty string.
	 */
	[[nodiscard]] static std::string ConvertVirtualPathToFilename(const std::string& VirtualPath);

	/**
	 * Absolute (or weakly-canonicalizable) disk path under a mount → virtual path.
	 * Unmounted → empty.
	 */
	[[nodiscard]] static std::string ConvertFilenameToVirtualPath(const std::string& Filename);

	/**
	 * Long package name (or soft object path) → absolute package file on disk.
	 * Appends GetPackageExtension() when missing.
	 */
	[[nodiscard]] static std::string ConvertPackageNameToFilename(const std::string& PackageOrObjectPath);

	/**
	 * Disk package file → long package name (extension stripped).
	 * Unmounted → empty.
	 */
	[[nodiscard]] static std::string ConvertFilenameToPackageName(const std::string& Filename);

private:
	static std::string ProjectDir;
	static std::string EngineDir;
	static std::string ProjectContentDir;
	static std::string EngineContentDir;
	static std::vector<FPathMount> MountPoints;
	static bool bInitialized;
};

} // namespace Maho
