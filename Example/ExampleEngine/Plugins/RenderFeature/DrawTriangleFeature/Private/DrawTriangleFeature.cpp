#include "DrawTriangleFeature.h"

#include <Log.h>

namespace Maho
{

namespace
{
	// Fullscreen triangle via gl_VertexIndex (no vertex buffer needed).
	constexpr const char* kVertexShader = R"(
#version 460
void main()
{
	vec2 pos[3] = vec2[](vec2(0.0, -0.5), vec2(0.5, 0.5), vec2(-0.5, 0.5));
	gl_Position = vec4(pos[gl_VertexIndex], 0.0, 1.0);
}
)";

	constexpr const char* kFragmentShader = R"(
#version 460
layout(location = 0) out vec4 OutColor;
void main()
{
	OutColor = vec4(1.0, 0.2, 0.2, 1.0);
}
)";
}

void FDrawTriangleFeature::Render(FRender& R)
{
	IRHI* RHI = R.GetRHI();
	FShaderCompilerServer* Compiler = R.GetShaderCompiler();
	if (RHI == nullptr || Compiler == nullptr)
	{
		return;
	}

	// Lazy one-time build: compile shaders + create the pipeline.
	if (!bBuilt)
	{
		FShaderCompileDesc VDesc;
		VDesc.Source = kVertexShader;
		VDesc.Stage = ERHIShaderStage::Vertex;
		VDesc.EntryPoint = "main";
		FShaderCompileResult VResult = FShaderCompilerServer::CompileStage(VDesc);
		if (!VResult.bSuccess)
		{
			MAHO_LOG_CORE_ERROR("DrawTriangleFeature: VS compile failed: {}", VResult.ErrorLog);
			return;
		}

		FShaderCompileDesc FDesc;
		FDesc.Source = kFragmentShader;
		FDesc.Stage = ERHIShaderStage::Fragment;
		FDesc.EntryPoint = "main";
		FShaderCompileResult FResult = FShaderCompilerServer::CompileStage(FDesc);
		if (!FResult.bSuccess)
		{
			MAHO_LOG_CORE_ERROR("DrawTriangleFeature: FS compile failed: {}", FResult.ErrorLog);
			return;
		}

		FRHIShaderModuleDesc VSDesc;
		VSDesc.Stage = ERHIShaderStage::Vertex;
		VSDesc.Bytecode = VResult.Bytecode.data();
		VSDesc.BytecodeSize = VResult.Bytecode.size() * sizeof(std::uint32_t);
		VSDesc.EntryPoint = "main";
		VS = RHI->CreateShaderModule(VSDesc);

		FRHIShaderModuleDesc FSDesc;
		FSDesc.Stage = ERHIShaderStage::Fragment;
		FSDesc.Bytecode = FResult.Bytecode.data();
		FSDesc.BytecodeSize = FResult.Bytecode.size() * sizeof(std::uint32_t);
		FSDesc.EntryPoint = "main";
		FS = RHI->CreateShaderModule(FSDesc);

		if (VS == nullptr || FS == nullptr)
		{
			MAHO_LOG_CORE_ERROR("DrawTriangleFeature: CreateShaderModule failed");
			return;
		}

		FRHIPipelineLayoutDesc LayoutDesc;   // empty layout (no descriptors)
		Layout = RHI->CreatePipelineLayout(LayoutDesc);
		if (Layout == nullptr)
		{
			MAHO_LOG_CORE_ERROR("DrawTriangleFeature: CreatePipelineLayout failed");
			return;
		}

		FRHIGraphicsPipelineDesc PipelineDesc;
		PipelineDesc.VertexShader = VS;
		PipelineDesc.FragmentShader = FS;
		PipelineDesc.VertexEntryPoint = "main";
		PipelineDesc.FragmentEntryPoint = "main";
		PipelineDesc.Layout = Layout;
		PipelineDesc.RenderPass = RHI->GetSwapchainRenderPass();
		PipelineDesc.Topology = ERHIPrimitiveTopology::TriangleList;
		PipelineDesc.VertexStride = 0;
		PipelineDesc.CullMode = ERHICullMode::None;
		PipelineDesc.FillMode = ERHIFillMode::Solid;

		Pipeline = RHI->CreateGraphicsPipeline(PipelineDesc);
		if (Pipeline == nullptr)
		{
			MAHO_LOG_CORE_ERROR("DrawTriangleFeature: CreateGraphicsPipeline failed");
			return;
		}

		bBuilt = true;
		MAHO_LOG_CORE_INFO("DrawTriangleFeature: triangle pipeline built");
	}

	RHI->DrawPrimitive(
		Pipeline, 3,
		RHI->GetFramebufferWidth(), RHI->GetFramebufferHeight(),
		0.15f, 0.25f, 0.45f, 1.0f);
}

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_DRAWTRIANGLEFEATURE_API Maho::FLayerBase* CreateLayer()
{
	return Maho::FDrawTriangleFeature::CreateLayer();
}
