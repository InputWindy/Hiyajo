#include <Render/Shader/ShaderCache.h>

#include <Core/System/Log.h>
#include <Core/System/Paths.h>
#include <Render/Shader/ShaderCompiler.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

namespace Maho
{

FShaderCache::FShaderCache(std::string CacheDir)
	: CacheRoot(std::move(CacheDir))
{
	if (!CacheRoot.empty() && CacheRoot.back() != '/' && CacheRoot.back() != '\\')
	{
		CacheRoot += '/';
	}
	CacheRoot += "Shaders/";
}

static void WriteBinaryFile(const std::string& Path, const void* Data, std::size_t Size)
{
	std::ofstream File(Path, std::ios::binary);
	if (!File)
	{
		MAHO_CORE_ERROR("FShaderCache: failed to write {}", Path);
		return;
	}
	File.write(static_cast<const char*>(Data), static_cast<std::streamsize>(Size));
}

static bool ReadBinaryFile(const std::string& Path, std::vector<std::uint32_t>& OutData)
{
	std::ifstream File(Path, std::ios::binary | std::ios::ate);
	if (!File)
	{
		return false;
	}

	std::streamsize Size = File.tellg();
	if (Size <= 0 || Size % 4 != 0)
	{
		return false;
	}

	File.seekg(0, std::ios::beg);
	OutData.resize(static_cast<std::size_t>(Size) / 4);
	File.read(reinterpret_cast<char*>(OutData.data()), Size);
	return File.good();
}

static std::string HashString(const std::string& Input)
{
	// Simple FNV-1a 64-bit hash
	std::uint64_t Hash = 14695981039346656037ULL;
	for (char C : Input)
	{
		Hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(C));
		Hash *= 1099511628211ULL;
	}
	char Buf[17];
	std::snprintf(Buf, sizeof(Buf), "%016llX", static_cast<unsigned long long>(Hash));
	return {Buf};
}

std::string FShaderCache::MakeKey(const std::string& Path,
                                  ERHIShaderStage Stage,
                                  const std::string& EntryPoint,
                                  const std::vector<std::string>& Defines)
{
	std::ostringstream Combined;
	Combined << Path << "|" << static_cast<int>(Stage) << "|" << EntryPoint;
	for (const auto& D : Defines)
	{
		Combined << "|" << D;
	}
	return HashString(Combined.str());
}

bool FShaderCache::TryLoad(const std::string& Key, std::vector<std::uint32_t>& OutBytecode)
{
	std::string Path = CacheRoot + Key + ".spv";
	return ReadBinaryFile(Path, OutBytecode);
}

bool FShaderCache::TryLoadReflection(const std::string& Key, FShaderReflection& OutReflection)
{
	std::string Path = CacheRoot + Key + ".json";
	std::ifstream File(Path);
	if (!File)
	{
		return false;
	}

	try
	{
		nlohmann::json J;
		File >> J;

		if (J.contains("uniform_blocks"))
		{
			for (const auto& BlockJ : J["uniform_blocks"])
			{
				FShaderUniformBlockInfo Info;
				Info.BlockName = BlockJ.value("name", "");
				Info.Set = BlockJ.value("set", 0);
				Info.Binding = BlockJ.value("binding", 0);
				Info.BlockSize = BlockJ.value("size", 0);
				Info.Stages = static_cast<ERHIShaderStage>(BlockJ.value("stages", 0));
				if (BlockJ.contains("members"))
				{
					for (const auto& MemJ : BlockJ["members"])
					{
						FShaderParamInfo Param;
						Param.Name = MemJ.value("name", "");
						Param.Offset = MemJ.value("offset", 0);
						Param.Size = MemJ.value("size", 0);
						Param.Set = Info.Set;
						Param.Binding = Info.Binding;
						Info.Members.push_back(Param);
					}
				}
				OutReflection.UniformBlocks.push_back(std::move(Info));
			}
		}

		if (J.contains("samplers"))
		{
			for (const auto& SamJ : J["samplers"])
			{
				FShaderSamplerInfo Info;
				Info.Name = SamJ.value("name", "");
				Info.Set = SamJ.value("set", 0);
				Info.Binding = SamJ.value("binding", 0);
				Info.Stages = static_cast<ERHIShaderStage>(SamJ.value("stages", 0));
				OutReflection.Samplers.push_back(std::move(Info));
			}
		}

		return true;
	}
	catch (const std::exception& E)
	{
		MAHO_CORE_WARN("FShaderCache: failed to parse reflection JSON: {}", E.what());
		return false;
	}
}

void FShaderCache::Store(const std::string& Key,
                         const std::vector<std::uint32_t>& Bytecode,
                         const FShaderReflection& Reflection)
{
	// Create directory if needed (simple approach: try to write, directory should exist)
	// The Cached/ directory is created by project setup scripts.

	// Write SPIR-V binary
	std::string SpvPath = CacheRoot + Key + ".spv";
	WriteBinaryFile(SpvPath, Bytecode.data(), Bytecode.size() * sizeof(std::uint32_t));

	// Write reflection as JSON
	nlohmann::json J;

	nlohmann::json BlocksJson = nlohmann::json::array();
	for (const auto& Block : Reflection.UniformBlocks)
	{
		nlohmann::json BlockJ;
		BlockJ["name"] = Block.BlockName;
		BlockJ["set"] = Block.Set;
		BlockJ["binding"] = Block.Binding;
		BlockJ["size"] = Block.BlockSize;
		BlockJ["stages"] = static_cast<int>(Block.Stages);

		nlohmann::json MembersJson = nlohmann::json::array();
		for (const auto& M : Block.Members)
		{
			nlohmann::json MemJ;
			MemJ["name"] = M.Name;
			MemJ["offset"] = M.Offset;
			MemJ["size"] = M.Size;
			MembersJson.push_back(MemJ);
		}
		BlockJ["members"] = MembersJson;
		BlocksJson.push_back(BlockJ);
	}
	J["uniform_blocks"] = BlocksJson;

	nlohmann::json SampJson = nlohmann::json::array();
	for (const auto& S : Reflection.Samplers)
	{
		nlohmann::json SamJ;
		SamJ["name"] = S.Name;
		SamJ["set"] = S.Set;
		SamJ["binding"] = S.Binding;
		SamJ["stages"] = static_cast<int>(S.Stages);
		SampJson.push_back(SamJ);
	}
	J["samplers"] = SampJson;

	std::string JsonPath = CacheRoot + Key + ".json";
	std::ofstream File(JsonPath);
	if (File)
	{
		File << J.dump(2);
	}
	else
	{
		MAHO_CORE_ERROR("FShaderCache: failed to write reflection JSON to {}", JsonPath);
	}

	MAHO_CORE_INFO("FShaderCache: stored key={}", Key);
}

} // namespace Maho
