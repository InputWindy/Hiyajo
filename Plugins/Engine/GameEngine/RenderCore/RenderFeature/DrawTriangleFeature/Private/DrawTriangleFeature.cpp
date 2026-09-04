#include "DrawTriangleFeature.h"

#include <Frame.h>
#include <Log.h>
#include <Scene.h>
#include <ShaderParameterStruct.h>

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

// Statics of FTriangleShader: expose the feature-private GLSL to the shader type.
const char* FTriangleShader::GetVertexSource()       { return kVertexShader; }
const char* FTriangleShader::GetFragmentSource()     { return kFragmentShader; }
const char* FTriangleShader::GetVertexEntryPoint()   { return "main"; }
const char* FTriangleShader::GetFragmentEntryPoint() { return "main"; }

// Compile-time FParameters for the triangle pass. The fullscreen triangle binds
// no descriptors and pushes no constants, so the param struct is empty; the macro
// engine still yields a valid (empty) layout AddPass consumes.
BEGIN_SHADER_PARAMETER_STRUCT(FTriangleParameters)
END_SHADER_PARAMETER_STRUCT()

FDrawTriangleFeature::FDrawTriangleFeature()
{
	// Draw AFTER FScene clears + submits: AddPass submits at the IRender call
	// site, so order this feature's IRender against FScene's IEndRender submit.
	MyStage<IRender>().IsWaiting<Scene::FScene>().ForStage<IEndRender>();
}

FDrawTriangleFeature::~FDrawTriangleFeature()
{
	// Nothing to own: the pipeline + layout live in FRender's PSO cache (owned by
	// the resource pool, destroyed at Shutdown), and the shader modules are owned by
	// the per-T TShaderHandle state. FRender tears its features down BEFORE
	// destroying the pool, so no raw RHI object outlives it.
}

void FDrawTriangleFeature::Render(FRender& R)
{
	Scene::FScene* Scene = Scene::GetScene();
	if (Scene == nullptr || !Scene->GetSceneColor().IsValid())
	{
		return;   // target not built yet
	}
	const std::uint32_t TargetW = Scene->GetSceneColor().GetWidth();
	const std::uint32_t TargetH = Scene->GetSceneColor().GetHeight();
	const ERHIFormat ColorFormat = Scene->GetSceneColor().GetFormat();

	// Fetch the shader modules (compile + sync) up front, so the resolved modules,
	// bytecode hashes and entry points can be written straight into the pipeline
	// config. Each frame returns the cached handle; Wait() is the sync-before-use.
	TShaderHandle<FTriangleShader> Shader = R.TryGetShader<FTriangleShader>();
	FRHIShaderModule* VS = nullptr;
	FRHIShaderModule* FS = nullptr;
	if (Shader.Wait())
	{
		VS = Shader.GetVertex();
		FS = Shader.GetFragment();
	}
	if (VS == nullptr || FS == nullptr)
	{
		MAHO_LOG_CORE_ERROR("DrawTriangleFeature: shader not ready");
		return;
	}

	// Pass declaration: the input binding (layout) + the output target, as one
	// unit given to AddPass. The descriptor layout is empty (no bindings); AddPass
	// builds it from the compile-time FTriangleParameters metadata.
	FRenderTarget Target;
	FRenderTarget::FAttachment Color;
	Color.View = Scene->GetSceneColor();
	Color.LoadOp = ERHILoadOp::Load;
	Color.StoreOp = ERHIStoreOp::Store;
	Target.AddColor(Color);
	if (Scene->GetSceneDepth().IsValid())
	{
		FRenderTarget::FAttachment Depth;
		Depth.View = Scene->GetSceneDepth();
		Depth.LoadOp = ERHILoadOp::Load;
		Depth.StoreOp = ERHIStoreOp::DontCare;
		Target.SetDepth(Depth);
	}
	Target.SetSize(TargetW, TargetH);

	// Pipeline config table: the state this feature chooses to configure PLUS the
	// shader modules it resolved above. AddPass fills the layout from Pass.Layout,
	// queries the PSO and binds it implicitly -- the feature only records the draws.
	FRHIGraphicsPipelineDesc PipelineDesc;
	PipelineDesc.VertexShader = VS;
	PipelineDesc.FragmentShader = FS;
	PipelineDesc.VertexShaderHash = Shader.GetVertexHash();
	PipelineDesc.FragmentShaderHash = Shader.GetFragmentHash();
	PipelineDesc.VertexEntryPoint = Detail::GetVertexEntryPoint<FTriangleShader>();
	PipelineDesc.FragmentEntryPoint = Detail::GetFragmentEntryPoint<FTriangleShader>();
	PipelineDesc.RenderPass = nullptr;   // dynamic rendering (formats below)
	PipelineDesc.Topology = ERHIPrimitiveTopology::TriangleList;
	PipelineDesc.VertexStride = 0;
	PipelineDesc.CullMode = ERHICullMode::None;
	PipelineDesc.FillMode = ERHIFillMode::Solid;
	PipelineDesc.ColorFormat = ColorFormat;
	PipelineDesc.DepthFormat = ERHIFormat::D32_SFLOAT;
	PipelineDesc.bDepthTest = true;
	PipelineDesc.bDepthWrite = false;

	// Consume FScene's hardcoded triangle DrawList: record its batch (Draw(3) -- no
	// VB, the vertex shader generates the primitive). One AddPass == one subpass, so
	// the list is already that subpass's batch set. The feature never knows who
	// produced the list.
	const FDrawList& Draws = Scene->GetTriangleDrawList();
	FTriangleParameters* Params = R.AllocParameters<FTriangleParameters>();
	R.AddPass(ERHICommandListType::Graphics, PipelineDesc, Target, Params,
		[&Draws, TargetW, TargetH](FRHICommandList& Cmd)
		{
			Cmd.SetViewport(0.0f, 0.0f, static_cast<float>(TargetW), static_cast<float>(TargetH));
			Cmd.SetScissor(0, 0, TargetW, TargetH);
			for (const auto& B : Draws.GetBatches())
			{
				if (B.VertexBuffer.IsValid())
				{
					Cmd.BindVertexBuffer(0, B.VertexBuffer.GetRHI(), B.VertexOffset);
				}
				if (B.IndexBuffer.IsValid())
				{
					Cmd.BindIndexBuffer(B.IndexBuffer.GetRHI(), B.IndexOffset, B.bIndex32);
					Cmd.DrawIndexed(B.IndexCount, B.InstanceCount, 0, 0, 0);
				}
				else if (B.VertexCount > 0)
				{
					Cmd.Draw(B.VertexCount, B.InstanceCount, B.VertexOffset, 0);
				}
			}
		});
}

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_DRAWTRIANGLEFEATURE_API Maho::FLayerBase* CreateLayer()
{
	return Maho::FDrawTriangleFeature::CreateLayer();
}
