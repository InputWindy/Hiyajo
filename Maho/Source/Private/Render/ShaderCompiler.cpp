#include <Render/Shader/ShaderCompiler.h>

#include <Core/Misc/Log.h>

#ifdef MAHO_WITH_GLSLANG
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>
#endif

#include <algorithm>
#include <cstring>

namespace Maho
{

bool FShaderCompiler::bInitialized = false;

bool FShaderCompiler::Initialize()
{
	if (bInitialized)
	{
		return true;
	}

#ifdef MAHO_WITH_GLSLANG
	glslang::InitializeProcess();
	bInitialized = true;
	MAHO_CORE_INFO("FShaderCompiler: glslang initialized");
	return true;
#else
	MAHO_CORE_ERROR("FShaderCompiler: glslang not available (MAHO_WITH_GLSLANG=0)");
	return false;
#endif
}

void FShaderCompiler::Shutdown()
{
#ifdef MAHO_WITH_GLSLANG
	if (bInitialized)
	{
		glslang::FinalizeProcess();
		bInitialized = false;
		MAHO_CORE_INFO("FShaderCompiler: glslang shutdown");
	}
#endif
}

#ifdef MAHO_WITH_GLSLANG

static EShLanguage ToGlslangStage(ERHIShaderStage Stage)
{
	switch (Stage)
	{
	case ERHIShaderStage::Vertex:
		return EShLangVertex;
	case ERHIShaderStage::Fragment:
		return EShLangFragment;
	case ERHIShaderStage::Compute:
		return EShLangCompute;
	default:
		return EShLangVertex;
	}
}

static TBuiltInResource GetDefaultBuiltInResource()
{
	TBuiltInResource R{};
	R.maxLights = 32;
	R.maxClipPlanes = 6;
	R.maxTextureUnits = 32;
	R.maxTextureCoords = 32;
	R.maxVertexAttribs = 64;
	R.maxVertexUniformComponents = 4096;
	R.maxVaryingFloats = 64;
	R.maxVertexTextureImageUnits = 32;
	R.maxCombinedTextureImageUnits = 80;
	R.maxTextureImageUnits = 32;
	R.maxFragmentUniformComponents = 4096;
	R.maxDrawBuffers = 32;
	R.maxVertexUniformVectors = 128;
	R.maxVaryingVectors = 8;
	R.maxFragmentUniformVectors = 16;
	R.maxVertexOutputVectors = 16;
	R.maxFragmentInputVectors = 15;
	R.minProgramTexelOffset = -8;
	R.maxProgramTexelOffset = 7;
	R.maxClipDistances = 8;
	R.maxComputeWorkGroupCountX = 65535;
	R.maxComputeWorkGroupCountY = 65535;
	R.maxComputeWorkGroupCountZ = 65535;
	R.maxComputeWorkGroupSizeX = 1024;
	R.maxComputeWorkGroupSizeY = 1024;
	R.maxComputeWorkGroupSizeZ = 64;
	R.maxComputeUniformComponents = 1024;
	R.maxComputeTextureImageUnits = 16;
	R.maxComputeImageUniforms = 8;
	R.maxComputeAtomicCounters = 8;
	R.maxComputeAtomicCounterBuffers = 1;
	R.maxVaryingComponents = 60;
	R.maxVertexOutputComponents = 64;
	R.maxGeometryInputComponents = 64;
	R.maxGeometryOutputComponents = 128;
	R.maxFragmentInputComponents = 128;
	R.maxImageUnits = 8;
	R.maxCombinedImageUnitsAndFragmentOutputs = 8;
	R.maxCombinedShaderOutputResources = 8;
	R.maxImageSamples = 0;
	R.maxVertexImageUniforms = 0;
	R.maxTessControlImageUniforms = 0;
	R.maxTessEvaluationImageUniforms = 0;
	R.maxGeometryImageUniforms = 0;
	R.maxFragmentImageUniforms = 8;
	R.maxCombinedImageUniforms = 8;
	R.maxGeometryTextureImageUnits = 16;
	R.maxGeometryOutputVertices = 256;
	R.maxGeometryTotalOutputComponents = 1024;
	R.maxGeometryUniformComponents = 1024;
	R.maxGeometryVaryingComponents = 64;
	R.maxTessControlInputComponents = 128;
	R.maxTessControlOutputComponents = 128;
	R.maxTessControlTextureImageUnits = 16;
	R.maxTessControlUniformComponents = 1024;
	R.maxTessControlTotalOutputComponents = 4096;
	R.maxTessEvaluationInputComponents = 128;
	R.maxTessEvaluationOutputComponents = 128;
	R.maxTessEvaluationTextureImageUnits = 16;
	R.maxTessEvaluationUniformComponents = 1024;
	R.maxTessPatchComponents = 120;
	R.maxPatchVertices = 32;
	R.maxTessGenLevel = 64;
	R.maxViewports = 16;
	R.maxVertexAtomicCounters = 0;
	R.maxTessControlAtomicCounters = 0;
	R.maxTessEvaluationAtomicCounters = 0;
	R.maxGeometryAtomicCounters = 0;
	R.maxFragmentAtomicCounters = 8;
	R.maxCombinedAtomicCounters = 8;
	R.maxAtomicCounterBindings = 1;
	R.maxVertexAtomicCounterBuffers = 0;
	R.maxTessControlAtomicCounterBuffers = 0;
	R.maxTessEvaluationAtomicCounterBuffers = 0;
	R.maxGeometryAtomicCounterBuffers = 0;
	R.maxFragmentAtomicCounterBuffers = 1;
	R.maxCombinedAtomicCounterBuffers = 1;
	R.maxAtomicCounterBufferSize = 16384;
	R.maxTransformFeedbackBuffers = 4;
	R.maxTransformFeedbackInterleavedComponents = 64;
	R.maxCullDistances = 8;
	R.maxCombinedClipAndCullDistances = 8;
	R.maxSamples = 4;
	R.maxMeshOutputVerticesNV = 256;
	R.maxMeshOutputPrimitivesNV = 512;
	R.maxMeshWorkGroupSizeX_NV = 32;
	R.maxMeshWorkGroupSizeY_NV = 1;
	R.maxMeshWorkGroupSizeZ_NV = 1;
	R.maxTaskWorkGroupSizeX_NV = 32;
	R.maxTaskWorkGroupSizeY_NV = 1;
	R.maxTaskWorkGroupSizeZ_NV = 1;
	R.maxMeshViewCountNV = 4;
	R.limits.nonInductiveForLoops = 1;
	R.limits.whileLoops = 1;
	R.limits.doWhileLoops = 1;
	R.limits.generalUniformIndexing = 1;
	R.limits.generalAttributeMatrixVectorIndexing = 1;
	R.limits.generalVaryingIndexing = 1;
	R.limits.generalSamplerIndexing = 1;
	R.limits.generalVariableIndexing = 1;
	R.limits.generalConstantMatrixVectorIndexing = 1;
	return R;
}

#endif // MAHO_WITH_GLSLANG

FShaderCompileResult FShaderCompiler::CompileStage(
	const FShaderCompileDesc& Desc,
	ERHIShaderStage Stage,
	const std::string& EntryPoint)
{
	FShaderCompileResult Result;
	Result.bSuccess = false;

#ifdef MAHO_WITH_GLSLANG
	if (!bInitialized)
	{
		Result.ErrorLog = "FShaderCompiler: not initialized";
		return Result;
	}

	if (Desc.Source.empty())
	{
		Result.ErrorLog = "FShaderCompiler: empty source";
		return Result;
	}

	if (EntryPoint.empty())
	{
		Result.ErrorLog = "FShaderCompiler: empty entry point";
		return Result;
	}

	EShLanguage GlslangStage = ToGlslangStage(Stage);

	// Rename the entry-point function to "main" in source so glslang can find it.
	// The shader parser uses vert_main/frag_main; we must rewrite to "main"
	// because setEntryPoint("main") requires the source function to also be "main".
	std::string SourceCopy = Desc.Source;
	{
		// Find the function declaration and rename it.
		std::string Search = EntryPoint + "(";
		std::size_t Pos = 0;
		while ((Pos = SourceCopy.find(Search, Pos)) != std::string::npos)
		{
			SourceCopy.replace(Pos, EntryPoint.size(), "main");
			Pos += 4; // "main"
		}
	}

	// Inject stage #define after #version (setPreamble inserts BEFORE #version, illegal).
	std::string Preamble;
	if (Stage == ERHIShaderStage::Vertex)
	{
		Preamble += "#define MAHO_SHADER_STAGE_VERTEX 1\n";
	}
	else if (Stage == ERHIShaderStage::Fragment)
	{
		Preamble += "#define MAHO_SHADER_STAGE_FRAGMENT 1\n";
	}
	for (const auto& Def : Desc.Defines)
	{
		Preamble += "#define " + Def + "\n";
	}
	if (!Preamble.empty())
	{
		// Find the #version line and insert after it (and after any #extension lines)
		std::size_t VerPos = SourceCopy.find("#version");
		if (VerPos != std::string::npos)
		{
			std::size_t InsertPos = SourceCopy.find('\n', VerPos);
			if (InsertPos != std::string::npos) InsertPos = InsertPos + 1;
			// Skip any #extension lines after #version
			while (InsertPos < SourceCopy.size() && SourceCopy.compare(InsertPos, 10, "#extension") == 0)
			{
				std::size_t NL = SourceCopy.find('\n', InsertPos);
				if (NL == std::string::npos) break;
				InsertPos = NL + 1;
			}
			SourceCopy.insert(InsertPos, Preamble);
		}
	}

	glslang::TShader Shader(GlslangStage);
	const char* SourcePtr = SourceCopy.c_str();
	int SourceLen = static_cast<int>(SourceCopy.size());
	Shader.setStringsWithLengths(&SourcePtr, &SourceLen, 1);
	// Pipeline stages bind SPIR-V entry "main" (see FVulkanRHI::CreateGraphicsPipeline).
	Shader.setEntryPoint("main");

	// Target Vulkan 1.0 + SPIR-V 1.0
	Shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
	Shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);

	TBuiltInResource Resources = GetDefaultBuiltInResource();

	if (!Shader.parse(&Resources, 460, false, EShMsgDefault))
	{
		Result.ErrorLog = Shader.getInfoLog();
		Result.ErrorLog += "\n";
		Result.ErrorLog += Shader.getInfoDebugLog();
		return Result;
	}

	glslang::TProgram Program;
	Program.addShader(&Shader);

	if (!Program.link(EShMsgDefault))
	{
		Result.ErrorLog = Program.getInfoLog();
		Result.ErrorLog += "\n";
		Result.ErrorLog += Program.getInfoDebugLog();
		return Result;
	}

	// Extract SPIR-V binary
	std::vector<std::uint32_t> Spirv;
	spv::SpvBuildLogger Logger;
	glslang::SpvOptions Options;
	Options.generateDebugInfo = true;
	Options.disableOptimizer = false;
	glslang::GlslangToSpv(*Program.getIntermediate(GlslangStage), Spirv, &Logger, &Options);

	Result.Bytecode = std::move(Spirv);
	Result.bSuccess = true;

	// TODO: Implement reflection extraction using glslang 14.x API.
	// The reflection APIs changed significantly in 14.x.
	// For now, caller should provide descriptor layouts directly.

	MAHO_CORE_INFO("FShaderCompiler: compiled stage={} entry={} bytecode_size={}",
	               static_cast<int>(Stage), EntryPoint, Result.Bytecode.size() * sizeof(std::uint32_t));
#else
	Result.ErrorLog = "FShaderCompiler: glslang not available (MAHO_WITH_GLSLANG=0)";
#endif

	return Result;
}

} // namespace Maho
