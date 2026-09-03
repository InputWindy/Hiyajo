#include "RenderResourcePool.h"

#include <cstring>

namespace Maho
{

namespace
{
	// Descriptor-identity helpers for the PSO cache. Equality is by DESCRIPTOR
	// VALUE, not native pointer: a later same-descriptor request must match the
	// cached native. Pipeline equality keys the shaders on their CONTENT
	// fingerprints (std::uint64 hashes) rather than the module pointers, because
	// shader modules are transient (rebuilt per compile) while the bytecode is
	// identical across passes.

	bool PushConstantEqual(const FRHIPushConstantRange& A, const FRHIPushConstantRange& B)
	{
		return A.Stages == B.Stages && A.Offset == B.Offset && A.Size == B.Size;
	}

	bool PipelineLayoutDescEqual(const FRHIPipelineLayoutDesc& A, const FRHIPipelineLayoutDesc& B)
	{
		if (A.SetLayouts.size() != B.SetLayouts.size())
		{
			return false;
		}
		for (std::size_t I = 0; I < A.SetLayouts.size(); ++I)
		{
			if (A.SetLayouts[I] != B.SetLayouts[I])
			{
				return false;
			}
		}
		if (A.PushConstants.size() != B.PushConstants.size())
		{
			return false;
		}
		for (std::size_t I = 0; I < A.PushConstants.size(); ++I)
		{
			if (!PushConstantEqual(A.PushConstants[I], B.PushConstants[I]))
			{
				return false;
			}
		}
		return true;
	}

	bool VertexAttributeEqual(const FRHIVertexAttribute& A, const FRHIVertexAttribute& B)
	{
		return A.Location == B.Location && A.Format == B.Format && A.Offset == B.Offset;
	}

	bool AttachmentBlendEqual(const FRHIAttachmentBlend& A, const FRHIAttachmentBlend& B)
	{
		return A.bBlend == B.bBlend
			&& A.SrcColorFactor == B.SrcColorFactor
			&& A.DstColorFactor == B.DstColorFactor
			&& A.SrcAlphaFactor == B.SrcAlphaFactor
			&& A.DstAlphaFactor == B.DstAlphaFactor
			&& A.ColorOp == B.ColorOp
			&& A.AlphaOp == B.AlphaOp;
	}

	bool DescriptorBindingEqual(const FRHIDescriptorBinding& A, const FRHIDescriptorBinding& B)
{
	return A.Binding == B.Binding && A.Type == B.Type && A.Count == B.Count
		&& A.Stages == B.Stages && A.bPartiallyBound == B.bPartiallyBound
		&& A.bVariableCount == B.bVariableCount;
}

bool DescriptorSetLayoutDescEqual(const FRHIDescriptorSetLayoutDesc& A, const FRHIDescriptorSetLayoutDesc& B)
{
	if (A.Bindings.size() != B.Bindings.size())
	{
		return false;
	}
	for (std::size_t I = 0; I < A.Bindings.size(); ++I)
	{
		if (!DescriptorBindingEqual(A.Bindings[I], B.Bindings[I]))
		{
			return false;
		}
	}
	return true;
}

bool SamplerDescEqual(const FRHISamplerDesc& A, const FRHISamplerDesc& B)
{
	return A.MinFilter == B.MinFilter && A.MagFilter == B.MagFilter
		&& A.AddressU == B.AddressU && A.AddressV == B.AddressV && A.AddressW == B.AddressW
		&& A.LodBias == B.LodBias && A.MinLod == B.MinLod && A.MaxLod == B.MaxLod;
}

bool GraphicsPipelineDescEqual(const FRHIGraphicsPipelineDesc& A, const FRHIGraphicsPipelineDesc& B)
	{
		if (A.VertexShaderHash != B.VertexShaderHash || A.FragmentShaderHash != B.FragmentShaderHash)
		{
			return false;
		}
		if (std::strcmp(A.VertexEntryPoint, B.VertexEntryPoint) != 0 ||
			std::strcmp(A.FragmentEntryPoint, B.FragmentEntryPoint) != 0)
		{
			return false;
		}
		if (A.Layout != B.Layout || A.RenderPass != B.RenderPass)
		{
			return false;
		}
		if (A.Topology != B.Topology || A.VertexStride != B.VertexStride ||
			A.CullMode != B.CullMode || A.FillMode != B.FillMode)
		{
			return false;
		}
		if (A.ColorFormat != B.ColorFormat || A.DepthFormat != B.DepthFormat ||
			A.SampleCount != B.SampleCount || A.DepthCompare != B.DepthCompare)
		{
			return false;
		}
		if (A.bDepthTest != B.bDepthTest || A.bDepthWrite != B.bDepthWrite ||
			A.bAlphaToCoverage != B.bAlphaToCoverage)
		{
			return false;
		}
		if (A.Attributes.size() != B.Attributes.size())
		{
			return false;
		}
		for (std::size_t I = 0; I < A.Attributes.size(); ++I)
		{
			if (!VertexAttributeEqual(A.Attributes[I], B.Attributes[I]))
			{
				return false;
			}
		}
		if (A.AttachmentBlends.size() != B.AttachmentBlends.size())
		{
			return false;
		}
		for (std::size_t I = 0; I < A.AttachmentBlends.size(); ++I)
		{
			if (!AttachmentBlendEqual(A.AttachmentBlends[I], B.AttachmentBlends[I]))
			{
				return false;
			}
		}
		return true;
	}
}

FRHITexture* FRDGTextureRef::GetRHI() const
{
	return Pool != nullptr ? Pool->GetTexture(*this) : nullptr;
}

FRHITextureView* FRDGTextureRef::GetView() const
{
	return Pool != nullptr ? Pool->GetTextureView(*this) : nullptr;
}

ERHIFormat FRDGTextureRef::GetFormat() const
{
	return Pool != nullptr ? Pool->GetTextureDesc(*this).Format : ERHIFormat::Unknown;
}

std::uint32_t FRDGTextureRef::GetWidth() const
{
	return Pool != nullptr ? Pool->GetTextureDesc(*this).Extent.Width : 0;
}

std::uint32_t FRDGTextureRef::GetHeight() const
{
	return Pool != nullptr ? Pool->GetTextureDesc(*this).Extent.Height : 0;
}

FRHIBuffer* FRDGBufferRef::GetRHI() const
{
	return Pool != nullptr ? Pool->GetBuffer(*this) : nullptr;
}

// -- slot allocation -----------------------------------------------------------

std::uint32_t FRenderResourcePool::AllocTextureSlot()
{
	if (!FreeTextureSlots.empty())
	{
		const std::uint32_t Slot = FreeTextureSlots.back();
		FreeTextureSlots.pop_back();
		return Slot;
	}
	Textures.emplace_back();
	return static_cast<std::uint32_t>(Textures.size() - 1);
}

std::uint32_t FRenderResourcePool::AllocBufferSlot()
{
	if (!FreeBufferSlots.empty())
	{
		const std::uint32_t Slot = FreeBufferSlots.back();
		FreeBufferSlots.pop_back();
		return Slot;
	}
	Buffers.emplace_back();
	return static_cast<std::uint32_t>(Buffers.size() - 1);
}

// -- reuse lookup --------------------------------------------------------------

std::int32_t FRenderResourcePool::FindReusableTexture(
	const FRHITextureDesc& Desc, ERDGResourceLifetime Lifetime) const
{
	const bool bTransient = (Lifetime == ERDGResourceLifetime::Transient);
	for (std::size_t I = 0; I < Textures.size(); ++I)
	{
		const FTextureEntry& E = Textures[I];
		// Same lifetime class + same descriptor + native alive (a recycled slot),
		// and not currently owned by anyone.
		if (E.Native != nullptr && (E.Lifetime == ERDGResourceLifetime::Transient) == bTransient && !E.bActive && E.Desc == Desc)
		{
			return static_cast<std::int32_t>(I);
		}
	}
	return -1;
}

std::int32_t FRenderResourcePool::FindReusableBuffer(
	const FRHIBufferDesc& Desc, ERDGResourceLifetime Lifetime) const
{
	const bool bTransient = (Lifetime == ERDGResourceLifetime::Transient);
	for (std::size_t I = 0; I < Buffers.size(); ++I)
	{
		const FBufferEntry& E = Buffers[I];
		if (E.Native != nullptr && (E.Lifetime == ERDGResourceLifetime::Transient) == bTransient && !E.bActive && E.Desc == Desc)
		{
			return static_cast<std::int32_t>(I);
		}
	}
	return -1;
}

// -- native destroy helpers ----------------------------------------------------

void FRenderResourcePool::DestroyTextureEntry(FTextureEntry& Entry)
{
	if (Entry.View)
	{
		RHI->DestroyTextureView(Entry.View);
		Entry.View = nullptr;
	}
	if (Entry.Native)
	{
		RHI->DestroyTexture(Entry.Native);
		Entry.Native = nullptr;
	}
}

void FRenderResourcePool::DestroyBufferEntry(FBufferEntry& Entry)
{
	if (Entry.Native)
	{
		RHI->DestroyBuffer(Entry.Native);
		Entry.Native = nullptr;
	}
}

// -- create --------------------------------------------------------------------

FRDGTextureRef FRenderResourcePool::CreateTexture(const FRHITextureDesc& Desc, ERDGResourceLifetime Lifetime)
{
	// Reuse first: an inactive slot with the same descriptor + lifetime class.
	// For a transient this is how we avoid per-frame vkCreate/vkAllocate (the
	// slot keeps its native + memory across frames and is merely re-activated).
	const std::int32_t Reuse = FindReusableTexture(Desc, Lifetime);
	if (Reuse >= 0)
	{
		FTextureEntry& E = Textures[static_cast<std::size_t>(Reuse)];
		E.bActive = true;
		E.RefCount = 1;
		return FRDGTextureRef(this, static_cast<std::uint32_t>(Reuse));
	}

	const std::uint32_t Slot = AllocTextureSlot();
	FTextureEntry& Entry = Textures[Slot];
	// A recycled slot (free list) may hold a stale native from a DIFFERENT
	// descriptor (transient desc changed, or a persistent slot that never matched).
	// Drop it before reusing so the pool never leaks a Vulkan object + memory.
	if (Entry.Native != nullptr || Entry.View != nullptr)
	{
		DestroyTextureEntry(Entry);
	}

	Entry.Desc = Desc;
	Entry.Lifetime = Lifetime;
	Entry.RefCount = 1;
	Entry.bActive = true;
	Entry.Native = RHI ? RHI->CreateTexture(Desc) : nullptr;

	if (Entry.Native != nullptr)
	{
		FRHITextureViewDesc ViewDesc;
		ViewDesc.Texture = Entry.Native;
		ViewDesc.Format = Desc.Format;
		ViewDesc.MipCount = Desc.MipLevels;
		ViewDesc.ArrayLayerCount = Desc.ArrayLayers;
		Entry.View = RHI->CreateTextureView(ViewDesc);
	}

	return FRDGTextureRef(this, Slot);
}

FRDGBufferRef FRenderResourcePool::CreateBuffer(const FRHIBufferDesc& Desc, ERDGResourceLifetime Lifetime)
{
	const std::int32_t Reuse = FindReusableBuffer(Desc, Lifetime);
	if (Reuse >= 0)
	{
		FBufferEntry& E = Buffers[static_cast<std::size_t>(Reuse)];
		E.bActive = true;
		E.RefCount = 1;
		return FRDGBufferRef(this, static_cast<std::uint32_t>(Reuse));
	}

	const std::uint32_t Slot = AllocBufferSlot();
	FBufferEntry& Entry = Buffers[Slot];
	if (Entry.Native != nullptr)
	{
		DestroyBufferEntry(Entry);
	}

	Entry.Desc = Desc;
	Entry.Lifetime = Lifetime;
	Entry.RefCount = 1;
	Entry.bActive = true;
	Entry.Native = RHI ? RHI->CreateBuffer(Desc) : nullptr;
	return FRDGBufferRef(this, Slot);
}

// -- command list --------------------------------------------------------------

FRHICommandList* FRenderResourcePool::AcquireRenderList()
{
	if (RHI == nullptr)
	{
		return nullptr;
	}
	FRHICommandList* List = RHI->CreateCommandList(ERHICommandListType::Graphics);
	if (List != nullptr)
	{
		std::lock_guard<std::mutex> Lock(RenderListsMutex);
		PendingRenderLists.push_back(List);
	}
	return List;
}

// -- PSO cache -----------------------------------------------------------------

FRHIShaderModule* FRenderResourcePool::GetOrCreateShaderModule(const FRHIShaderModuleDesc& Desc)
{
	// Shader module identity is the CONTENT (bytecode copy + stage + entry point),
	// not the pointer: modules are rebuilt from identical bytes across passes and
	// must share one native. The bytecode is copied so the lookup does not depend
	// on the caller's buffer outliving a match.
	const std::size_t WordCount = Desc.BytecodeSize / sizeof(std::uint32_t);
	for (const FShaderModuleEntry& E : ShaderModules)
	{
		if (E.Native == nullptr || E.Stage != Desc.Stage)
		{
			continue;
		}
		if (std::strcmp(E.EntryPoint.c_str(), Desc.EntryPoint) != 0)
		{
			continue;
		}
		if (E.Bytecode.size() != WordCount)
		{
			continue;
		}
		if (WordCount == 0 || std::memcmp(E.Bytecode.data(), Desc.Bytecode, Desc.BytecodeSize) == 0)
		{
			return E.Native;
		}
	}
	FRHIShaderModule* Native = RHI ? RHI->CreateShaderModule(Desc) : nullptr;
	if (Native != nullptr)
	{
		FShaderModuleEntry Entry;
		Entry.Stage = Desc.Stage;
		Entry.EntryPoint = Desc.EntryPoint;
		const auto* Words = static_cast<const std::uint32_t*>(Desc.Bytecode);
		Entry.Bytecode.assign(Words, Words + WordCount);
		Entry.Native = Native;
		ShaderModules.push_back(std::move(Entry));
	}
	return Native;
}

FRHIPipelineLayout* FRenderResourcePool::GetOrCreatePipelineLayout(const FRHIPipelineLayoutDesc& Desc)
{
	for (const FPipelineLayoutEntry& E : PipelineLayouts)
	{
		if (E.Native != nullptr && PipelineLayoutDescEqual(E.Desc, Desc))
		{
			return E.Native;
		}
	}
	FRHIPipelineLayout* Native = RHI ? RHI->CreatePipelineLayout(Desc) : nullptr;
	if (Native != nullptr)
	{
		PipelineLayouts.push_back({ Desc, Native });
	}
	return Native;
}

FRHIDescriptorSetLayout* FRenderResourcePool::GetOrCreateDescriptorSetLayout(const FRHIDescriptorSetLayoutDesc& Desc)
{
	for (const FDescriptorSetLayoutEntry& E : DescriptorSetLayouts)
	{
		if (E.Native != nullptr && DescriptorSetLayoutDescEqual(E.Desc, Desc))
		{
			return E.Native;
		}
	}
	FRHIDescriptorSetLayout* Native = RHI ? RHI->CreateDescriptorSetLayout(Desc) : nullptr;
	if (Native != nullptr)
	{
		DescriptorSetLayouts.push_back({ Desc, Native });
	}
	return Native;
}

FRHIGraphicsPipeline* FRenderResourcePool::GetOrCreateGraphicsPipeline(const FRHIGraphicsPipelineDesc& Desc)
{
	for (const FGraphicsPipelineEntry& E : GraphicsPipelines)
	{
		if (E.Native != nullptr && GraphicsPipelineDescEqual(E.Desc, Desc))
		{
			return E.Native;
		}
	}
	FRHIGraphicsPipeline* Native = RHI ? RHI->CreateGraphicsPipeline(Desc) : nullptr;
	if (Native != nullptr)
	{
		GraphicsPipelines.push_back({ Desc, Native });
	}
	return Native;
}

// -- pool-owned sampler / descriptor set ----------------------------------------

FRHISampler* FRenderResourcePool::CreateSampler(const FRHISamplerDesc& Desc)
{
	for (const FSamplerEntry& E : Samplers)
	{
		if (E.Native != nullptr && SamplerDescEqual(E.Desc, Desc))
		{
			return E.Native;
		}
	}
	FRHISampler* Native = RHI ? RHI->CreateSampler(Desc) : nullptr;
	if (Native != nullptr)
	{
		FSamplerEntry Entry;
		Entry.Desc = Desc;
		Entry.Native = Native;
		Samplers.push_back(std::move(Entry));
	}
	return Native;
}

FRHIDescriptorSet* FRenderResourcePool::CreateDescriptorSet(
	FRHIDescriptorSetLayout* Layout, const FRHIDescriptorSetLayoutDesc& LayoutDesc)
{
	if (Layout == nullptr)
	{
		return nullptr;
	}
	FRHIDescriptorPoolDesc PoolDesc;
	PoolDesc.MaxSets = 1;
	for (const FRHIDescriptorBinding& B : LayoutDesc.Bindings)
	{
		FRHIDescriptorPoolSize Size;
		Size.Type = B.Type;
		Size.Count = B.Count;
		PoolDesc.PoolSizes.push_back(Size);
	}
	FRHIDescriptorPool* Pool = RHI ? RHI->CreateDescriptorPool(PoolDesc) : nullptr;
	if (Pool == nullptr)
	{
		return nullptr;
	}
	FRHIDescriptorSet* Set = RHI ? RHI->AllocateDescriptorSet(Pool, Layout) : nullptr;
	if (Set == nullptr)
	{
		RHI->DestroyDescriptorPool(Pool);
		return nullptr;
	}
	FDescriptorSetEntry Entry;
	Entry.Pool = Pool;
	Entry.Set = Set;
	DescriptorSets.push_back(Entry);
	return Set;
}

// -- release -------------------------------------------------------------------

void FRenderResourcePool::ReleaseTexture(FRDGTextureRef& Ref)
{
	if (!Ref.IsValid())
	{
		return;
	}
	FTextureEntry& E = Textures[Ref.Id];
	if (E.RefCount > 0)
	{
		E.RefCount -= 1;
	}
	if (E.RefCount == 0)
	{
		E.bActive = false;
		// Native + view are KEPT (both lifetimes) so a later same-descriptor
		// request reuses them. Transients are additionally recycled by BeginFrame.
	}
	Ref.Reset();
}

void FRenderResourcePool::ReleaseBuffer(FRDGBufferRef& Ref)
{
	if (!Ref.IsValid())
	{
		return;
	}
	FBufferEntry& E = Buffers[Ref.Id];
	if (E.RefCount > 0)
	{
		E.RefCount -= 1;
	}
	if (E.RefCount == 0)
	{
		E.bActive = false;
	}
	Ref.Reset();
}

// -- resolve -------------------------------------------------------------------

FRHITexture* FRenderResourcePool::GetTexture(const FRDGTextureRef& Ref) const
{
	if (!Ref.IsValid() || Ref.Id >= Textures.size())
	{
		return nullptr;
	}
	return Textures[Ref.Id].Native;
}

FRHITextureView* FRenderResourcePool::GetTextureView(const FRDGTextureRef& Ref)
{
	if (!Ref.IsValid() || Ref.Id >= Textures.size())
	{
		return nullptr;
	}
	return Textures[Ref.Id].View;
}

const FRHITextureDesc& FRenderResourcePool::GetTextureDesc(const FRDGTextureRef& Ref) const
{
	static const FRHITextureDesc Empty{};
	return (!Ref.IsValid() || Ref.Id >= Textures.size()) ? Empty : Textures[Ref.Id].Desc;
}

FRHIBuffer* FRenderResourcePool::GetBuffer(const FRDGBufferRef& Ref) const
{
	if (!Ref.IsValid() || Ref.Id >= Buffers.size())
	{
		return nullptr;
	}
	return Buffers[Ref.Id].Native;
}

// -- frame recycle -------------------------------------------------------------

void FRenderResourcePool::BeginFrame()
{
	// Destroy the previous frame's submitted command lists first. They were
	// submitted in their IEndRender stages; the host BeginFrame already waited the
	// swapchain fence, so no list is still executing. This is the fence-aligned
	// lifetime boundary shared with transient resource recycling below.
	std::vector<FRHICommandList*> Lists;
	{
		std::lock_guard<std::mutex> Lock(RenderListsMutex);
		Lists.swap(PendingRenderLists);
	}
	for (FRHICommandList* List : Lists)
	{
		if (RHI)
		{
			RHI->DestroyCommandList(List);
		}
	}

	// Recycle EVERY transient slot (active or not): mark inactive and free the
	// slot. Crucially we KEEP native + view + memory - the host BeginFrame already
	// waited the previous frame's fence, so no in-flight command references them,
	// and a same-descriptor request this frame reuses the objects in place (no
	// per-frame vkCreate / vkAllocate). A later request whose descriptor differs
	// drops the stale native via Create* ('recycled slot, different desc' branch).
	// Persistent slots are never touched here (cross-frame, reclaimed only on
	// Shutdown or a later descriptor-reuse miss).
	FreeTextureSlots.clear();
	for (std::size_t I = 0; I < Textures.size(); ++I)
	{
		FTextureEntry& E = Textures[I];
		if (E.Lifetime == ERDGResourceLifetime::Transient)
		{
			E.bActive = false;
			E.RefCount = 0;
			FreeTextureSlots.push_back(static_cast<std::uint32_t>(I));
		}
	}

	FreeBufferSlots.clear();
	for (std::size_t I = 0; I < Buffers.size(); ++I)
	{
		FBufferEntry& E = Buffers[I];
		if (E.Lifetime == ERDGResourceLifetime::Transient)
		{
			E.bActive = false;
			E.RefCount = 0;
			FreeBufferSlots.push_back(static_cast<std::uint32_t>(I));
		}
	}
}

// -- shutdown ------------------------------------------------------------------

void FRenderResourcePool::Shutdown()
{
	for (FRHICommandList* List : PendingRenderLists)
	{
		if (RHI)
		{
			RHI->DestroyCommandList(List);
		}
	}
	PendingRenderLists.clear();

	// PSO cache: destroy pipelines BEFORE the shader modules they reference, and
	// shader modules BEFORE the pipeline layouts / descriptor set layouts. Each
	// native must outlive everything that referenced it, so the order is:
	// graphics pipelines -> shader modules -> pipeline layouts -> descriptor set
	// layouts.
	for (FGraphicsPipelineEntry& E : GraphicsPipelines)
	{
		if (E.Native && RHI)
		{
			RHI->DestroyGraphicsPipeline(E.Native);
		}
	}
	GraphicsPipelines.clear();
	for (FShaderModuleEntry& E : ShaderModules)
	{
		if (E.Native && RHI)
		{
			RHI->DestroyShaderModule(E.Native);
		}
	}
	ShaderModules.clear();
	for (FPipelineLayoutEntry& E : PipelineLayouts)
	{
		if (E.Native && RHI)
		{
			RHI->DestroyPipelineLayout(E.Native);
		}
	}
	PipelineLayouts.clear();
	for (FDescriptorSetLayoutEntry& E : DescriptorSetLayouts)
	{
		if (E.Native && RHI)
		{
			RHI->DestroyDescriptorSetLayout(E.Native);
		}
	}
	DescriptorSetLayouts.clear();

	// Pool-owned sampler + descriptor sets. A descriptor set references its pool (it
	// is allocated from it), the texture view it was written with, and the sampler
	// it binds -- so sets/pools are destroyed BEFORE textures and samplers, and the
	// samplers before textures (a descriptor write references the sampler, not the
	// other way around). Free the set, then the pool, for each owned pair.
	for (FDescriptorSetEntry& E : DescriptorSets)
	{
		if (E.Pool && RHI)
		{
			if (E.Set)
			{
				RHI->FreeDescriptorSet(E.Pool, E.Set);
				E.Set = nullptr;
			}
			RHI->DestroyDescriptorPool(E.Pool);
			E.Pool = nullptr;
		}
	}
	DescriptorSets.clear();
	for (FSamplerEntry& E : Samplers)
	{
		if (E.Native && RHI)
		{
			RHI->DestroySampler(E.Native);
			E.Native = nullptr;
		}
	}
	Samplers.clear();

	for (FTextureEntry& E : Textures)
	{
		DestroyTextureEntry(E);
	}
	Textures.clear();
	FreeTextureSlots.clear();

	for (FBufferEntry& E : Buffers)
	{
		DestroyBufferEntry(E);
	}
	Buffers.clear();
	FreeBufferSlots.clear();
}

} // namespace Maho
