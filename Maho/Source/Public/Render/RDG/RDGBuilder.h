#pragma once

#include <Core/Export.h>
#include <Render/RDG/RDGPass.h>
#include <Render/RDG/RDGResources.h>
#include <Render/RDG/RDGTransientPool.h>
#include <Render/RHI/RHICommandList.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace Maho
{

class IRHI;
class FRHIServer;
class FRDGBuffer;
class FRDGTexture;

/** CPU-side parameter block owned by the graph builder. */
struct MAHO_API FRDGPassParameters
{
	struct FRenderTargetBinding
	{
		FRDGTexture* Texture = nullptr;
		FRHITextureView* View = nullptr;
		ERHILoadOp LoadOp = ERHILoadOp::Clear;
		ERHIStoreOp StoreOp = ERHIStoreOp::Store;
		float ClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	};

	using FAccessPair = std::pair<FRDGResource*, ERHIResourceState>;

	std::vector<FAccessPair> Reads;
	std::vector<FAccessPair> Writes;
	std::vector<FRenderTargetBinding> RenderTargets;
};

/**
 * Render Graph Builder - single-frame pass graph compiler + executor.
 *
 * Usage:
 *   auto& Params = GB.AllocateParameters();
 *   Params.Reads  = { {buf, ERHIResourceState::VertexBuffer} };
 *   Params.Writes = { {tex, ERHIResourceState::RenderTarget} };
 *   Params.RenderTargets = { {tex, view, Clear, Store, {0,0,0,1}} };
 *   GB.AddRasterPass("MyPass", Params, [](FRHICommandList& Cmd){ ... });
 */
class MAHO_API FRDGBuilder
{
public:
	using FAccessPair = FRDGPassParameters::FAccessPair;

	FRDGBuilder() = default;
	explicit FRDGBuilder(IRHI* InRHI);
	~FRDGBuilder();
	FRDGBuilder(const FRDGBuilder&) = delete;
	FRDGBuilder& operator=(const FRDGBuilder&) = delete;

	/**
	 * Reuse this builder for a new frame: set the RHI (via its owning server)
	 * and clear all per-frame state (passes, resources, barriers). Transient
	 * pool slots are released for reuse but their backing Vk resources persist.
	 */
	void Reset(FRHIServer* InRHIServer);

	// -- Resource registration --

	FRDGBuffer* RegisterExternalBuffer(FRHIBuffer* Buffer,
	                                   ERHIResourceState InitialState,
	                                   const char* Name);
	FRDGTexture* RegisterExternalTexture(FRHITexture* Texture,
	                                     ERHIResourceState InitialState,
	                                     const char* Name);

	// -- Transient resource creation --

	FRDGBuffer* CreateBuffer(const FRHIBufferDesc& Desc, const char* Name);
	FRDGTexture* CreateTexture(const FRHITextureDesc& Desc, const char* Name);

	// -- Cross-feature resource exchange (same stage only) --

	void Export(FRDGResource* Resource, const char* Name);
	[[nodiscard]] FRDGResource* Import(const char* Name) const;

	// -- Parameter block allocation --

	FRDGPassParameters& AllocateParameters();

	// -- Pass declaration (step-by-step) --

	FRDGPass& AddRasterPass(const char* Name);
	FRDGPass& AddComputePass(const char* Name);

	// -- Pass declaration (parameter-based - recommended) --

	FRDGPass& AddRasterPass(const char* Name,
	                        const FRDGPassParameters& Params,
	                        FRDGPass::FExecuteFunc Execute);

	FRDGPass& AddComputePass(const char* Name,
	                         const FRDGPassParameters& Params,
	                         FRDGPass::FExecuteFunc Execute);

	// -- Resource access declaration (step-by-step only) --

	void Read(FRDGPass& Pass, FRDGResource* Resource, ERHIResourceState State);
	void Write(FRDGPass& Pass, FRDGResource* Resource, ERHIResourceState State);

	// -- UBO upload (declarative) --

	void UploadBuffer(FRDGBuffer* DstBuffer, const void* Data, std::size_t Size);

	// -- Frame finalize (terminal step, runs after all passes + transitions) --

	/**
	 * Optional per-frame terminal step (e.g. ImGui composite + swapchain
	 * present). Executed once at the end of Execute(), after every graph pass
	 * and after final external-resource transitions. Cleared on Reset().
	 */
	void SetFrameFinalize(std::function<void()> Func);

	// -- Compilation & execution --

	void Compile();
	void Execute();

	// -- Queries --

	[[nodiscard]] FRDGResource* GetResource(const char* Name) const;
	[[nodiscard]] std::size_t GetPassCount() const { return Passes.size(); }

private:
	FRHIServer* RHIServer = nullptr;
	IRHI* RHI = nullptr;

	std::vector<std::unique_ptr<FRDGResource>> OwnedResources;
	std::vector<std::unique_ptr<FRDGPassParameters>> ParameterPool;

	std::vector<std::unique_ptr<FRDGPass>> OwnedPasses;
	std::vector<FRDGPass*> Passes;

	std::unordered_map<std::string, FRDGResource*> NamedResources;
	std::unordered_map<std::string, FRDGResource*> ExportedResources;
	FRDGTransientPool TransientPool;

	/** External resources that need a final transition back to their initial state. */
	std::vector<std::tuple<FRDGResource*, ERHIResourceState, ERHIResourceState>> FinalTransitions;

	/** Terminal per-frame step (ImGui + present); runs last in Execute(). */
	std::function<void()> FrameFinalize;

	struct FCompiledPass
	{
		FRDGPass* Pass;
		std::vector<std::pair<FRDGResource*, ERHIResourceState>> PreBarriers;
	};
	std::vector<FCompiledPass> CompiledPasses;

	// -- Compile helpers --

	void CollectResourceLifetimes();
	void AllocateTransientResources();
	void SortPasses();
	void DeriveBarriers();

	struct FResourceLifetime
	{
		FRDGResource* Resource = nullptr;
		std::uint32_t FirstUse = 0;
		std::uint32_t LastUse = 0;
	};
	std::unordered_map<FRDGResource*, FResourceLifetime> Lifetimes;

	void BuildRenderingAttachments(const FRDGPassParameters* Params,
		                               std::vector<FRHIRenderingAttachmentInfo>& OutColor,
		                               FRHIRenderingAttachmentInfo& OutDepth,
		                               std::uint32_t& OutWidth, std::uint32_t& OutHeight);

	// -- Execute helpers (run on the RHI thread) --

	void ExecutePassesAndTransitions();
	void RunFrameFinalize();
};

} // namespace Maho
