#pragma once

#include <Core/Misc/Export.h>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Maho
{

struct FMaterialParamValue
{
	std::vector<std::uint8_t> RawData;
	std::string TextureAsset;  // SoftPath string — resolved by project-side ResourceSystem
};

struct FCaseInsensitiveHash
{
	std::size_t operator()(const std::string& S) const;
};

struct FCaseInsensitiveEqual
{
	bool operator()(const std::string& A, const std::string& B) const;
};

class MAHO_API FMaterialParamMap
{
public:
	void SetFloat(const char* Name, float V);
	void SetFloat2(const char* Name, float X, float Y);
	void SetFloat3(const char* Name, float X, float Y, float Z);
	void SetFloat4(const char* Name, float X, float Y, float Z, float W);
	void SetTexture(const char* Name, const std::string& Path);

	const FMaterialParamValue* Find(const std::string& Name) const;

	void ForEach(std::function<void(const std::string&, const FMaterialParamValue&)> Fn) const;

	[[nodiscard]] bool IsEmpty() const { return Values.empty(); }

private:
	std::unordered_map<std::string, FMaterialParamValue, FCaseInsensitiveHash, FCaseInsensitiveEqual> Values;
};

} // namespace Maho