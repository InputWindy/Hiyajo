#pragma once

#include <Core/Export.h>
#include <Render/RHI/RHIEnums.h>
#include <Render/Shader/ShaderCompiler.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace Maho
{

class MAHO_API FShaderCache
{
public:
	explicit FShaderCache(std::string CacheRoot);
	~FShaderCache();

	bool TryLoad(const std::string& Key, std::vector<std::uint32_t>& Bytecode);
	void Store(const std::string& Key, const std::vector<std::uint32_t>& Bytecode,
	           const FShaderReflection& Reflection);
	bool TryLoadReflection(const std::string& Key, FShaderReflection& OutReflection);

	static std::string MakeKey(const std::string& ShaderPath,
	                           ERHIShaderStage Stage,
	                           const std::string& EntryPoint,
	                           const std::vector<std::string>& Defines);

private:
	std::string CacheRoot;
	std::unordered_map<std::string, std::vector<std::uint32_t>> MemCache;
	std::unordered_map<std::string, FShaderReflection> ReflectionCache;

	std::string MakeFilePath(const std::string& Key) const;
};

} // namespace Maho
