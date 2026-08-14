#pragma once

#include <Core/Export.h>
#include <Render/RHI/RHIEnums.h>
#include <Render/RHI/RHIResources.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Maho
{

// ─── Shader property types (from Properties{} block) ───────

enum class EShaderPropertyType : std::uint8_t
{
	Float,
	Color,
	Int,
	Texture2D,
	TextureCube,
	Range,
};

struct FShaderProperty
{
	std::string Name;
	std::string DisplayName;
	EShaderPropertyType Type = EShaderPropertyType::Float;

	// Default value: Color → float[4], Float → float, Range → float with min/max
	float DefaultFloat[4] = {0.f, 0.f, 0.f, 0.f};
	float MinValue = 0.f;
	float MaxValue = 1.f;
};

// ─── Render state from SubShader / Pass blocks ───────

struct FShaderRenderState
{
	ERHICullMode CullMode = ERHICullMode::Back;
	ERHIFillMode FillMode = ERHIFillMode::Solid;
	bool bDepthWrite = true;
	ERHICompareOp DepthCompare = ERHICompareOp::Less;
	bool bDepthTest = true;
	ERHIBlendFactor SrcBlend = ERHIBlendFactor::One;
	ERHIBlendFactor DstBlend = ERHIBlendFactor::Zero;
	ERHIBlendFactor SrcAlphaBlend = ERHIBlendFactor::One;
	ERHIBlendFactor DstAlphaBlend = ERHIBlendFactor::Zero;
	ERHIBlendOp ColorBlendOp = ERHIBlendOp::Add;
	ERHIBlendOp AlphaBlendOp = ERHIBlendOp::Add;
	bool bBlendEnabled = false;
	bool bAlphaToCoverage = false;
	std::uint8_t ColorWriteMask = 0xF; // RGBA
};

// ─── Vertex semantic → location mapping ───────

enum class EShaderVertexSemantic : std::uint8_t
{
	Position = 0,
	Normal = 1,
	TexCoord0 = 2,
	TexCoord1 = 3,
	Tangent = 4,
	Color0 = 5,
	BoneIndices = 13,
	BoneWeights = 14,
};

// ─── Reflection types ───────

struct FShaderParamInfo
{
	std::string Name;
	std::uint32_t Offset = 0;
	std::uint32_t Size = 0;
	ERHIDescriptorType Type = ERHIDescriptorType::UniformBuffer;
	std::uint32_t Set = 0;
	std::uint32_t Binding = 0;
};

struct FShaderUniformBlockInfo
{
	std::string BlockName;
	std::uint32_t Set = 0;
	std::uint32_t Binding = 0;
	std::uint32_t BlockSize = 0;
	ERHIShaderStage Stages = ERHIShaderStage::None;
	std::vector<FShaderParamInfo> Members;
};

struct FShaderSamplerInfo
{
	std::string Name;
	std::uint32_t Set = 0;
	std::uint32_t Binding = 0;
	ERHIShaderStage Stages = ERHIShaderStage::None;
};

struct FShaderVertexAttribute
{
	std::string SemanticName;
	EShaderVertexSemantic Semantic = EShaderVertexSemantic::Position;
	std::uint32_t Location = 0;
	std::uint32_t Binding = 0;
	ERHIFormat Format = ERHIFormat::Unknown;
};

struct FShaderReflection
{
	std::vector<FShaderUniformBlockInfo> UniformBlocks;
	std::vector<FShaderSamplerInfo> Samplers;
	std::vector<FRHIPushConstantRange> PushConstants;
	std::vector<FShaderVertexAttribute> VertexAttributes;
	std::vector<FShaderProperty> Properties;
	FShaderRenderState RenderState;

	// Inter-stage varying count (deduced from v2f struct)
	std::uint32_t VaryingCount = 0;

	// Per‑pass bytecode hash (FNV-1a of merged vert+ frag SPIR‑V)
	std::uint64_t BytecodeHash = 0;
};

// ─── Per‑pass compiled data ───────

struct FShaderPassCompiled
{
	std::string PassName;
	std::string ShaderPath;
	std::vector<std::uint32_t> VertexBytecode;
	std::vector<std::uint32_t> FragmentBytecode;
	std::vector<std::uint32_t>* VertexSpv = nullptr;
	std::vector<std::uint32_t>* FragmentSpv = nullptr;

	// Descriptor layout info
	std::vector<FShaderUniformBlockInfo> Set0Blocks; // Frame
	std::vector<FShaderUniformBlockInfo> Set1Blocks; // Object
	std::vector<FShaderUniformBlockInfo> Set2Blocks; // Material
	std::vector<FShaderSamplerInfo> Set2Samplers;

	FShaderRenderState RenderState;
	std::vector<FShaderVertexAttribute> VertexAttributes;
	std::uint32_t VaryingCount = 0;
	std::uint64_t BytecodeHash = 0;
};

// ─── Compile descriptor ───────

struct FShaderCompileDesc
{
	std::string Source;                    // Pass GLSL source only (after preprocess)
	std::string VertexEntry;               // Vertex entry function name
	std::string FragmentEntry;             // Fragment entry function name
	std::vector<std::string> Defines;
};

struct FShaderCompileResult
{
	bool bSuccess = false;
	std::vector<std::uint32_t> Bytecode;
	FShaderReflection Reflection;
	std::string ErrorLog;
};

// ─── Shader Database (per‑pass de‑duplication) ───────

class MAHO_API FShaderDatabase
{
public:
	/** Load a .shader file and extract all passes with compiled SPIR‑V. */
	bool LoadShader(const std::string& ShaderPath,
	                const std::vector<std::string>& ShaderSearchPaths,
	                const std::vector<std::string>& IncludePaths,
	                const std::string& CacheDir);

	/** Get compiled pass by (ShaderPath, PassName). */
	const FShaderPassCompiled* FindPass(const std::string& ShaderPath,
	                                    const std::string& PassName) const;

	/** De‑duplicated pass index (keyed by bytecode hash). */
	const FShaderPassCompiled* FindPassByHash(std::uint64_t BytecodeHash) const;

	/** All passes for per‑stage iteration. */
	const std::vector<FShaderPassCompiled>& GetAllPasses() const { return Passes; }

private:
	std::vector<FShaderPassCompiled> Passes;

	// BytecodeHash → index into Passes (de‑duplication index)
	std::unordered_map<std::uint64_t, std::size_t> HashToIndex;

	// ShaderPath → pass index list
	std::unordered_map<std::string, std::vector<std::size_t>> ShaderToPasses;

	// (ShaderPath, PassName) → pass index
	std::unordered_map<std::string, std::size_t> KeyToPass;
};

// ─── Compiler ───────

class MAHO_API FShaderCompiler
{
public:
	static bool Initialize();
	static void Shutdown();

	static FShaderCompileResult CompileStage(
		const FShaderCompileDesc& Desc,
		ERHIShaderStage Stage,
		const std::string& EntryPoint);

private:
	static bool bInitialized;
};

} // namespace Maho
