#include "DrawTriangleFeature.h"

#include <Log.h>
#include <Scene.h>

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

FDrawTriangleFeature::FDrawTriangleFeature()
{
	// Draw AFTER FScene clears the shared scene targets.
	AddDependency(std::type_index(typeid(IRender)), "FScene", std::type_index(typeid(IRender)));
}

void FDrawTriangleFeature::Render(FRender& R)
{
	IRHI* RHI = R.GetRHI();
	FShaderCompilerServer* Compiler = R.GetShaderCompiler();
	if (RHI == nullptr || Compiler == nullptr)
	{
		return;
	}

	// Locate the global scene feature for its shared color/depth targets.
	Scene::FScene* Scene = Scene::GetScene();
	if (Scene == nullptr || !Scene->GetSceneColor().IsValid())
	{
		return;
	}

	// Lazy one-time build: compile shaders + create the pipeline (dynamic
	// rendering - the pipeline declares formats, no render pass).
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
		PipelineDesc.RenderPass = nullptr;   // dynamic rendering (formats below)
		PipelineDesc.Topology = ERHIPrimitiveTopology::TriangleList;
		PipelineDesc.VertexStride = 0;
		PipelineDesc.CullMode = ERHICullMode::None;
		PipelineDesc.FillMode = ERHIFillMode::Solid;
		PipelineDesc.ColorFormat = R.GetSwapchainFormat();
		PipelineDesc.DepthFormat = ERHIFormat::D32_SFLOAT;
		PipelineDesc.bDepthTest = true;
		PipelineDesc.bDepthWrite = false;

		Pipeline = RHI->CreateGraphicsPipeline(PipelineDesc);
		if (Pipeline == nullptr)
		{
			MAHO_LOG_CORE_ERROR("DrawTriangleFeature: CreateGraphicsPipeline failed");
			return;
		}

		bBuilt = true;
		MAHO_LOG_CORE_INFO("DrawTriangleFeature: triangle pipeline built");
	}

	// Record into the shared scene color target (Load - FScene already cleared).
	FRHICommandList* Cmd = R.GetFrameCommandList();
	if (Cmd == nullptr)
	{
		return;
	}

	FRHIRenderingAttachmentInfo Color;
	Color.View = Scene->GetSceneColor().GetView();
	Color.LoadOp = ERHILoadOp::Load;
	Color.StoreOp = ERHIStoreOp::Store;

	FRHIRenderingAttachmentInfo Depth;
	const FRHIRenderingAttachmentInfo* PDepth = nullptr;
	if (Scene->GetSceneDepth().IsValid())
	{
		Depth.View = Scene->GetSceneDepth().GetView();
		Depth.LoadOp = ERHILoadOp::Load;
		Depth.StoreOp = ERHIStoreOp::DontCare;
		PDepth = &Depth;
	}

	Cmd->BeginRendering(&Color, 1, PDepth, R.GetFramebufferWidth(), R.GetFramebufferHeight());
	Cmd->BindGraphicsPipeline(Pipeline);
	Cmd->SetViewport(0.0f, 0.0f,
		static_cast<float>(R.GetFramebufferWidth()),
		static_cast<float>(R.GetFramebufferHeight()));
	Cmd->SetScissor(0, 0, R.GetFramebufferWidth(), R.GetFramebufferHeight());
	Cmd->Draw(3);
	Cmd->EndRendering();
}

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_DRAWTRIANGLEFEATURE_API Maho::FLayerBase* CreateLayer()
{
	return Maho::FDrawTriangleFeature::CreateLayer();
}
