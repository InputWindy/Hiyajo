#include <Shader/MaterialParamMap.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace Maho
{

std::size_t FCaseInsensitiveHash::operator()(const std::string& S) const
{
	std::size_t Hash = 0;
	constexpr std::size_t FnvPrime = 1099511628211ULL;
	for (char C : S)
	{
		Hash ^= static_cast<std::size_t>(static_cast<unsigned char>(std::tolower(C)));
		Hash *= FnvPrime;
	}
	return Hash;
}

bool FCaseInsensitiveEqual::operator()(const std::string& A, const std::string& B) const
{
	if (A.size() != B.size())
	{
		return false;
	}
	for (std::size_t i = 0; i < A.size(); ++i)
	{
		if (std::tolower(static_cast<unsigned char>(A[i])) != std::tolower(static_cast<unsigned char>(B[i])))
		{
			return false;
		}
	}
	return true;
}

void FMaterialParamMap::SetFloat(const char* Name, float V)
{
	FMaterialParamValue Val;
	Val.RawData.resize(sizeof(float));
	std::memcpy(Val.RawData.data(), &V, sizeof(float));
	Values[Name] = std::move(Val);
}

void FMaterialParamMap::SetFloat2(const char* Name, float X, float Y)
{
	FMaterialParamValue Val;
	Val.RawData.resize(2 * sizeof(float));
	std::memcpy(Val.RawData.data(), &X, sizeof(float));
	std::memcpy(Val.RawData.data() + sizeof(float), &Y, sizeof(float));
	Values[Name] = std::move(Val);
}

void FMaterialParamMap::SetFloat3(const char* Name, float X, float Y, float Z)
{
	FMaterialParamValue Val;
	Val.RawData.resize(3 * sizeof(float));
	std::memcpy(Val.RawData.data(), &X, sizeof(float));
	std::memcpy(Val.RawData.data() + sizeof(float), &Y, sizeof(float));
	std::memcpy(Val.RawData.data() + 2 * sizeof(float), &Z, sizeof(float));
	Values[Name] = std::move(Val);
}

void FMaterialParamMap::SetFloat4(const char* Name, float X, float Y, float Z, float W)
{
	FMaterialParamValue Val;
	Val.RawData.resize(4 * sizeof(float));
	std::memcpy(Val.RawData.data(), &X, sizeof(float));
	std::memcpy(Val.RawData.data() + sizeof(float), &Y, sizeof(float));
	std::memcpy(Val.RawData.data() + 2 * sizeof(float), &Z, sizeof(float));
	std::memcpy(Val.RawData.data() + 3 * sizeof(float), &W, sizeof(float));
	Values[Name] = std::move(Val);
}

void FMaterialParamMap::SetTexture(const char* Name, const std::string& Path)
{
	FMaterialParamValue Val;
	Val.TextureAsset = Path;
	Values[Name] = std::move(Val);
}

const FMaterialParamValue* FMaterialParamMap::Find(const std::string& Name) const
{
	auto It = Values.find(Name);
	return (It != Values.end()) ? &It->second : nullptr;
}

void FMaterialParamMap::ForEach(std::function<void(const std::string&, const FMaterialParamValue&)> Fn) const
{
	for (const auto& [Key, Val] : Values)
	{
		Fn(Key, Val);
	}
}

} // namespace Maho
