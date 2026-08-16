#pragma once

#include <RenderFeature.h>
#include <RenderPipelineStage.h>
#include <UI/ImGuiSystem.h>
#include <Shader/ShaderCompiler.h>

#include "GPUScene.h"
#include "ForwardSceneTypes.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Maho
{

class FRenderSystem;
class IRHI;
class FWorld;
class FShaderDatabase;
struct FShaderPassCompiled;

class FRHIBuffer;
class FRHIShaderModule;
class FRHIDescriptorSetLayout;
class FRHIPipelineLayout;
class FRHIComputePipeline;
class FRHIGraphicsPipeline;
class FRHITexture;
class FRHITextureView;
class FRHIDescriptorPool;
class FRHIDescriptorSet;

/**
 * Per-frame game context for the forward renderer.
 * Gathered on the game thread from the ECS world.
 */
struct FForwardDrawContext : IGameContextSlice
{
	std::vector<FSceneDrawItem> Draws;
	FCameraFrameData Camera;

	void Gather(FWorld& World);
};

/**
 * GPU-driven forward renderer.
 *
 * Frame flow (BasePass stage):
 *   1. Upload scene instances to a storage buffer (CPU → GPU).
 *   2. Fill the indirect args buffer with empty (InstanceCount=0) commands.
 *   3. Compute pass: frustum-cull instances, write visible draw commands pass: vkCmdDrawIndexedIndirect draws visible instances.
 *   5. Vertex shader reads per-instance LocalToWorld / Color from the scene SSBO.
 */
class FForwardRendererFeature final : public TRenderFeatureWithContext<FForwardRendererFeature, FForwardDrawContext>,
	public TFeatureDependsPack<
			TFeatureDependsOn<ERenderPipelineStage::BeginFrame>,
			TFeatureDependsOn<ERenderPipelineStage::BasePass>
	>
{
public:
	FForwardRendererFeature();
	~FForwardRendererFeature() override;

	bool OnRegister(FRenderSystem& RenderSystem) override;
	void OnUnregister(FRenderSystem& RenderSystem) override;
	void ExecuteStage(ERenderPipelineStage Stage, const FForwardDrawContext& Context, FFrameContext& FrameCtx, FRDGBuilder& GB);

	/** Mark shader dirty so next frame recompiles (hot-reload from editor). */
	void MarkShaderDirty()
	{
		Ptr->bShaderReady = false;
	}

private:
	static constexpr int FrameRingSize = 3;

	struct FImpl
	{
		bool bInitialized = false;
		bool bShaderReady = false;
		IRHI* RHI = nullptr;
		FRenderSystem* RenderSystem = nullptr;

		// ── GPU Scene (ring buffer — one slot per frame in flight) ──
		FRHIBuffer* FrameUniformBuf[FrameRingSize] = {};    // view/proj UBO
		FRHIBuffer* ObjectUniformBuf[FrameRingSize] = {};   // per-object LocalToWorld UBO
		FRHIBuffer* CubeVBO = nullptr;                      // static unit-cube vertices
		FRHIBuffer* CubeIBO = nullptr;                      // static unit-cube indices

		std::uint32_t CubeIndexCount = 0;

		// ── Viewport ──
		FRHITexture* ViewportTex = nullptr;
		FRHITextureView* ViewportTexView = nullptr;
		FImGuiTextureHandle GameViewImGuiTexture;

		// ── Pipelines + descriptors ──
		FRHIGraphicsPipeline* DrawPipeline = nullptr;
		FRHIPipelineLayout* DrawLayout = nullptr;
		FRHIDescriptorPool* DescPool = nullptr;
		FRHIDescriptorSet* DrawDescSet[FrameRingSize] = {};   // object UBO
		FRHIDescriptorSet* FrameDescSet[FrameRingSize] = {};  // frame UBO

		std::uint32_t VpWidth = 600;
		std::uint32_t VpHeight = 400;
	};

	std::unique_ptr<FImpl> Ptr;

	bool SetupPersistentResources(FRenderSystem& RenderSystem);
	bool EnsureShaderReady();
	void DestroyShaderResources();
	void BuildCubeGeometry();
	void ExecuteBeginFrame(const FForwardDrawContext& Context, FFrameContext& FrameCtx, FRDGBuilder& GB);
	void ExecuteBasePass(const FForwardDrawContext& Context, FFrameContext& FrameCtx, FRDGBuilder& GB);
	void ComputeFrustumPlanes(const struct FCameraFrameData& Camera, float Aspect, FGPUCullParams& OutParams);
};

} // namespace Maho
