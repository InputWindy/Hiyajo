#pragma once


#include <Core/Export.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Maho
{

MAHO_API std::string ApplyModuleExtension(std::string_view BaseName);

class MAHO_API FAssembly
{
public:
	FAssembly();
	explicit FAssembly(std::string_view Path);
	~FAssembly();

	FAssembly(const FAssembly&) = delete;
	FAssembly& operator=(const FAssembly&) = delete;
	FAssembly(FAssembly&&) noexcept;
	FAssembly& operator=(FAssembly&&) noexcept;

	bool Load(std::string_view Path);

	void Unload();

	[[nodiscard]] bool IsLoaded() const;

	[[nodiscard]] void* GetProcAddress(const char* Name) const;

	template <typename TFunction>
	[[nodiscard]] TFunction GetProcAs(const char* Name) const
	{
		return reinterpret_cast<TFunction>(GetProcAddress(Name));
	}

	static bool LoadManifest();

	[[nodiscard]] static bool IsManifestLoaded();

	[[nodiscard]] static const std::vector<std::string>& GetChildren(std::string_view LayerName);

	[[nodiscard]] static const std::string& GetEngineRoot();

	[[nodiscard]] static std::filesystem::path ExecutableDir();

private:
	struct FImpl;
	std::unique_ptr<FImpl> Impl;
};

}
