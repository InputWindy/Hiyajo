#pragma once

#include "RenderApi.h"
#include <Shader/ShaderCompiler.h>

#include <string>
#include <vector>

namespace Maho
{

class FShaderCache;

// ─── Shader loader: parse .shader to extract Properties / SubShader / Pass ───

/** One parsed shader pass (entry points + preprocessed GLSL + render state). */
struct FShaderPassSource
{
	std::string PassName;
	std::string VertEntry;
	std::string FragEntry;
	std::string GlslSource;
	FShaderRenderState RenderState;
	std::vector<FShaderVertexAttribute> VertexInputs;
	std::uint32_t VaryingCount = 0;
};

/** A parsed .shader file: material properties + global render state + passes. */
struct FShaderFile
{
	std::string ShaderPath;
	std::vector<FShaderProperty> Properties;
	std::vector<FShaderPassSource> Passes;
	FShaderRenderState GlobalRenderState;
};

/** Static parser for Unity-style .shader source (Properties / SubShader / Pass). */
class MAHO_RENDER_API FShaderParser
{
public:
	static FShaderFile Parse(const std::string& FullPath,
	                         const std::vector<std::string>& IncludePaths);

	static std::string ResolveInclude(const std::string& IncludeName,
	                                  const std::vector<std::string>& IncludePaths);
	static std::string PreprocessIncludes(const std::string& Source,
	                                      const std::vector<std::string>& IncludePaths);

private:
	static void ParseProperties(const std::string& Source, std::size_t& Pos,
	                            std::vector<FShaderProperty>& OutProps);
	static void ParseSubShader(const std::string& Source, std::size_t& Pos,
	                           FShaderFile& OutFile,
	                           const std::vector<std::string>& IncludePaths);
	static void ParseRenderState(const std::string& Line, FShaderRenderState& OutState);
	static void ParseCombinedStructs(const std::string& PassSource,
	                                 const std::string& VertEntry,
	                                 const std::string& FragEntry,
	                                 std::vector<FShaderVertexAttribute>& OutAttrs,
	                                 std::string& OutGLSL,
	                                 std::uint32_t& OutVaryingCount);
	static void ParseA2VStruct(const std::string& PassSource,
	                           std::vector<FShaderVertexAttribute>& OutAttrs,
	                           std::string& OutInjectedGLSL);
	static void ParseVertexInputs(const std::string& PassSource,
	                              const std::string& VertEntry,
	                              std::vector<FShaderVertexAttribute>& OutAttrs,
	                              std::string& OutInjectedGLSL);
	static void ParseV2FStruct(const std::string& PassSource,
	                           std::string& OutInjectedGLSL,
	                           std::uint32_t& OutVaryingCount);
	static std::string GenerateMaterialUniforms(const std::vector<FShaderProperty>& Properties);
};

// ─── Legacy (existing code) ───

/** Compiled vertex + fragment pair for one shader path. */
struct FShaderPackage
{
	FShaderCompileResult Vertex;
	FShaderCompileResult Fragment;
};

/** Loads and compiles .shader files through the cache, resolving includes. */
class MAHO_RENDER_API FShaderLoader
{
public:
	explicit FShaderLoader(FShaderCache& Cache,
	                       std::vector<std::string> SearchPaths,
	                       std::vector<std::string> IncludePaths);

	FShaderPackage LoadShader(const std::string& ShaderPath);

private:
	FShaderCache* CachePtr;
	std::vector<std::string> ShaderPaths;
	std::vector<std::string> IncludePaths;

	void ParseShaderSource(const std::string& RawSource, FShaderCompileDesc& OutDesc);
	std::string LocateFile(const std::string& RelativePath);
	std::string ResolveInclude(const std::string& IncludeName);
	std::string ReadFile(const std::string& Path);
	std::string PreprocessIncludes(const std::string& Source);
};

FRHIDescriptorSetLayoutDesc BuildDescriptorSetLayoutFromReflection(
	const FShaderReflection& Reflection);

FRHIPipelineLayoutDesc BuildPipelineLayoutFromReflection(
	const FShaderReflection& VertexReflection,
	const FShaderReflection& FragmentReflection);

} // namespace Maho
