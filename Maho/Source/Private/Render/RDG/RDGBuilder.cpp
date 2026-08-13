#include <Render/RDG/RDGBuilder.h>

#include <Core/System/Log.h>
#include <Render/RHI/RHI.h>

#include <algorithm>
#include <cstring>
#include <queue>

namespace Maho
{

FRDGBuilder::FRDGBuilder(IRHI* InRHI)
	: RHI(InRHI)
{
}

FRDGBuilder::~FRDGBuilder()
{
}

// Resource Registration

FRDGBuffer* FRDGBuilder::RegisterExternalBuffer(
	FRHIBuffer* Buffer,
	ERHIResourceState InitialState,
	const char* Name)
{
	auto* Res = new FRDGBuffer(Name, Buffer->GetDesc(), true, false);
	Res->SetRHI(Buffer);
	Res->CurrentState = InitialState;
	OwnedResources.emplace_back(Res);
	NamedResources[Name] = Res;
	return Res;
}

FRDGTexture* FRDGBuilder::RegisterExternalTexture(
	FRHITexture* Texture,
	ERHIResourceState InitialState,
	const char* Name)
{
	auto* Res = new FRDGTexture(Name, Texture->GetDesc(), true, false);
	Res->SetRHI(Texture);
	Res->CurrentState = InitialState;
	OwnedResources.emplace_back(Res);
	NamedResources[Name] = Res;
	return Res;
}

// Transient Resource Creation

FRDGBuffer* FRDGBuilder::CreateBuffer(const FRHIBufferDesc& Desc, const char* Name)
{
	auto* Res = new FRDGBuffer(Name, Desc, false, true);
	OwnedResources.emplace_back(Res);
	NamedResources[Name] = Res;
	return Res;
}

FRDGTexture* FRDGBuilder::CreateTexture(const FRHITextureDesc& Desc, const char* Name)
{
	auto* Res = new FRDGTexture(Name, Desc, false, true);
	OwnedResources.emplace_back(Res);
	NamedResources[Name] = Res;
	return Res;
}

// Cross-Feature Export / Import

void FRDGBuilder::Export(FRDGResource* Resource, const char* Name)
{
	if (Resource == nullptr) return;
	ExportedResources[Name] = Resource;
	NamedResources[Name] = Resource;
}

FRDGResource* FRDGBuilder::Import(const char* Name) const
{
	auto It = ExportedResources.find(Name);
	return It != ExportedResources.end() ? It->second : nullptr;
}

// Pass Declaration (step-by-step, no Layer)

FRDGPass& FRDGBuilder::AddRasterPass(const char* Name)
{
	auto P = std::make_unique<FRDGPass>(Name, ERDGPassType::Raster);
	FRDGPass& Ref = *P;
	Passes.push_back(P.get());
	OwnedPasses.push_back(std::move(P));
	return Ref;
}

FRDGPass& FRDGBuilder::AddComputePass(const char* Name)
{
	auto P = std::make_unique<FRDGPass>(Name, ERDGPassType::Compute);
	FRDGPass& Ref = *P;
	Passes.push_back(P.get());
	OwnedPasses.push_back(std::move(P));
	return Ref;
}

FRDGPassParameters& FRDGBuilder::AllocateParameters()
{
	auto P = std::make_unique<FRDGPassParameters>();
	FRDGPassParameters& Ref = *P;
	ParameterPool.push_back(std::move(P));
	return Ref;
}

FRDGPass& FRDGBuilder::AddRasterPass(
	const char* Name,
	const FRDGPassParameters& Params,
	FRDGPass::FExecuteFunc Execute)
{
	auto P = std::make_unique<FRDGPass>(Name, ERDGPassType::Raster);
	for (const auto& [Res, State] : Params.Reads)
		P->AddRead(Res, State);
	for (const auto& [Res, State] : Params.Writes)
		P->AddWrite(Res, State);
	P->SetParameters(&Params);
	P->SetExecute(std::move(Execute));
	FRDGPass& Ref = *P;
	Passes.push_back(P.get());
	OwnedPasses.push_back(std::move(P));
	return Ref;
}

FRDGPass& FRDGBuilder::AddComputePass(
	const char* Name,
	const FRDGPassParameters& Params,
	FRDGPass::FExecuteFunc Execute)
{
	auto P = std::make_unique<FRDGPass>(Name, ERDGPassType::Compute);
	for (const auto& [Res, State] : Params.Reads)
		P->AddRead(Res, State);
	for (const auto& [Res, State] : Params.Writes)
		P->AddWrite(Res, State);
	P->SetParameters(&Params);
	P->SetExecute(std::move(Execute));
	FRDGPass& Ref = *P;
	Passes.push_back(P.get());
	OwnedPasses.push_back(std::move(P));
	return Ref;
}

void FRDGBuilder::Read(FRDGPass& Pass, FRDGResource* Resource, ERHIResourceState State)
{
	Pass.AddRead(Resource, State);
}

void FRDGBuilder::Write(FRDGPass& Pass, FRDGResource* Resource, ERHIResourceState State)
{
	Pass.AddWrite(Resource, State);
}

// UBO upload

void FRDGBuilder::UploadBuffer(FRDGBuffer* DstBuffer, const void* Data, std::size_t Size)
{
	if (Data == nullptr || Size == 0) return;

	// Staging buffer (host-visible), CPU writes, then a Copy pass uploads to the destination.
	FRHIBufferDesc StageDesc;
	StageDesc.Size = Size;
	StageDesc.Usage = ERHIBufferUsage::TransferSrc;
	StageDesc.MemoryUsage = ERHIMemoryUsage::CPUToGPU;
	FRHIBuffer* Staging = RHI->CreateBuffer(StageDesc);
	if (!Staging)
	{
		MAHO_CORE_ERROR("FRDGBuilder::UploadBuffer: staging create failed ({} bytes)", Size);
		return;
	}
	RHI->UpdateBuffer(Staging, 0, Size, Data);
	UploadStagingBuffers.push_back(Staging);

	auto& Params = AllocateParameters();
	Params.Writes.push_back({DstBuffer, ERHIResourceState::CopyDst});

	auto ExecuteFn = [Staging, DstBuffer, Size](FRHICommandList& Cmd)
	{
		if (FRHIBuffer* RHIBuf = DstBuffer->GetRHI())
		{
			Cmd.CopyBuffer(Staging, 0, RHIBuf, 0, Size);
		}
	};

	AddComputePass("Upload", Params, std::move(ExecuteFn));
}

FRDGResource* FRDGBuilder::GetResource(const char* Name) const
{
	auto It = NamedResources.find(Name);
	return It != NamedResources.end() ? It->second : nullptr;
}

// Compile

void FRDGBuilder::Compile()
{
	CompiledPasses.clear();
	TransientPool.Reset();
	if (Passes.empty()) return;
	CollectResourceLifetimes();
	AllocateTransientResources();
	SortPasses();
	DeriveBarriers();
}

void FRDGBuilder::CollectResourceLifetimes()
{
	Lifetimes.clear();
	for (auto& Res : OwnedResources)
	{
		FRDGResource* R = Res.get();
		FResourceLifetime LT;
		LT.Resource = R;
		if (R->IsExternal()) { LT.FirstUse = 0; LT.LastUse = UINT32_MAX; }
		else { LT.FirstUse = UINT32_MAX; LT.LastUse = 0; }
		Lifetimes[R] = LT;
	}
	for (std::size_t i = 0; i < Passes.size(); ++i)
	{
		FRDGPass* Pass = Passes[i];
		std::uint32_t Idx = static_cast<std::uint32_t>(i);
		for (const auto& Acc : Pass->GetReads())
		{
			auto It = Lifetimes.find(Acc.Resource);
			if (It != Lifetimes.end() && !Acc.Resource->IsExternal())
			{
				if (Idx < It->second.FirstUse) It->second.FirstUse = Idx;
				if (Idx > It->second.LastUse)  It->second.LastUse = Idx;
			}
		}
		for (const auto& Acc : Pass->GetWrites())
		{
			auto It = Lifetimes.find(Acc.Resource);
			if (It != Lifetimes.end() && !Acc.Resource->IsExternal())
			{
				if (Idx < It->second.FirstUse) It->second.FirstUse = Idx;
				if (Idx > It->second.LastUse)  It->second.LastUse = Idx;
			}
		}
	}
}

void FRDGBuilder::AllocateTransientResources()
{
	for (auto& Pair : Lifetimes)
	{
		FRDGResource* Res = Pair.first;
		FResourceLifetime& LT = Pair.second;
		if (!Res->IsTransient()) continue;
		if (LT.FirstUse > LT.LastUse) continue;
		if (auto* Buf = dynamic_cast<FRDGBuffer*>(Res))
		{
			FRHIBuffer* RHIRes = TransientPool.AllocateBuffer(RHI, Buf->GetDesc(), LT.FirstUse, LT.LastUse);
			Buf->SetRHI(RHIRes);
		}
		else if (auto* Tex = dynamic_cast<FRDGTexture*>(Res))
		{
			FRHITexture* RHIRes = TransientPool.AllocateTexture(RHI, Tex->GetDesc(), LT.FirstUse, LT.LastUse);
			Tex->SetRHI(RHIRes);
		}
	}
}

void FRDGBuilder::SortPasses()
{
	std::unordered_map<FRDGResource*, std::size_t> LastWritePass;
	std::unordered_map<FRDGPass*, std::vector<FRDGPass*>> Deps;
	std::unordered_map<FRDGPass*, int32_t> InDegree;

	for (auto* P : Passes) InDegree.try_emplace(P, 0);

	for (std::size_t i = 0; i < Passes.size(); ++i)
	{
		FRDGPass* Pass = Passes[i];
		for (const auto& ReadAcc : Pass->GetReads())
		{
			auto It = LastWritePass.find(ReadAcc.Resource);
			if (It != LastWritePass.end() && It->second < i)
			{
				FRDGPass* Pred = Passes[It->second];
				Deps[Pred].push_back(Pass);
				InDegree[Pass]++;
			}
		}
		for (const auto& WriteAcc : Pass->GetWrites())
		{
			LastWritePass[WriteAcc.Resource] = i;
		}
	}

	// Topological sort by dependency only (no Layer tie-break).
	std::vector<FRDGPass*> Sorted;
	std::queue<FRDGPass*> Ready;
	for (auto* P : Passes) if (InDegree[P] == 0) Ready.push(P);

	while (!Ready.empty())
	{
		FRDGPass* P = Ready.front();
		Ready.pop();
		Sorted.push_back(P);
		for (auto* Next : Deps[P])
			if (--InDegree[Next] == 0) Ready.push(Next);
	}

	if (Sorted.size() != Passes.size())
	{
		MAHO_CORE_WARN("FRDGBuilder: circular dependency, fallback to declaration order");
		Sorted = Passes;
	}
	Passes = std::move(Sorted);
}

void FRDGBuilder::DeriveBarriers()
{
	CompiledPasses.clear();
	FinalTransitions.clear();
	std::unordered_map<FRDGResource*, ERHIResourceState> CurrentStates;

	for (FRDGPass* Pass : Passes)
	{
		FCompiledPass CP;
		CP.Pass = Pass;

		for (const auto& ReadAcc : Pass->GetReads())
		{
			FRDGResource* Res = ReadAcc.Resource;
			if (Res == nullptr) continue;
			ERHIResourceState Cur = Res->CurrentState;
			auto It = CurrentStates.find(Res);
			if (It != CurrentStates.end()) Cur = It->second;
			if (Cur != ReadAcc.RequiredState && ReadAcc.RequiredState != ERHIResourceState::Common)
			{
				CP.PreBarriers.push_back({Res, ReadAcc.RequiredState});
				CurrentStates[Res] = ReadAcc.RequiredState;
			}
		}

		for (const auto& WriteAcc : Pass->GetWrites())
		{
			FRDGResource* Res = WriteAcc.Resource;
			if (Res == nullptr) continue;
			ERHIResourceState Cur = Res->CurrentState;
			auto It = CurrentStates.find(Res);
			if (It != CurrentStates.end()) Cur = It->second;
			if (Cur != WriteAcc.RequiredState && WriteAcc.RequiredState != ERHIResourceState::Common)
			{
				CP.PreBarriers.push_back({Res, WriteAcc.RequiredState});
				CurrentStates[Res] = WriteAcc.RequiredState;
			}
		}

		CompiledPasses.push_back(std::move(CP));
	}

	// Final transitions: external resources must return to their initial state
	// (e.g. a viewport texture rendered as RenderTarget, then sampled by ImGui).
	for (const auto& [Res, FinalState] : CurrentStates)
	{
		if (Res->IsExternal() && Res->CurrentState != FinalState)
		{
			FinalTransitions.push_back({Res, FinalState, Res->CurrentState});
		}
	}
}

void FRDGBuilder::BuildRenderingAttachments(
	const FRDGPassParameters* Params,
	std::vector<FRHIRenderingAttachmentInfo>& OutColor,
	FRHIRenderingAttachmentInfo& OutDepth,
	std::uint32_t& OutWidth, std::uint32_t& OutHeight)
{
	OutColor.clear();
	OutDepth = {};
	OutWidth = 0;
	OutHeight = 0;

	if (Params == nullptr || Params->RenderTargets.empty()) return;

	for (const auto& RT : Params->RenderTargets)
	{
		if (RT.Texture == nullptr || RT.Texture->GetRHI() == nullptr) continue;

		OutWidth = RT.Texture->GetDesc().Extent.Width;
		OutHeight = RT.Texture->GetDesc().Extent.Height;

		FRHIRenderingAttachmentInfo AttInfo{};
		AttInfo.View = RT.View;
		AttInfo.LoadOp = RT.LoadOp;
		AttInfo.StoreOp = RT.StoreOp;
		AttInfo.ClearColor[0] = RT.ClearColor[0];
		AttInfo.ClearColor[1] = RT.ClearColor[1];
		AttInfo.ClearColor[2] = RT.ClearColor[2];
		AttInfo.ClearColor[3] = RT.ClearColor[3];
		OutColor.push_back(AttInfo);
	}
}

// Execute

void FRDGBuilder::Execute()
{
	if (CompiledPasses.empty()) return;

	for (FCompiledPass& CP : CompiledPasses)
	{
		FRDGPass* Pass = CP.Pass;
		if (Pass == nullptr || !Pass->GetExecute()) continue;

		ERHICommandListType CmdType =
			(Pass->GetType() == ERDGPassType::Compute)
				? ERHICommandListType::Compute
				: ERHICommandListType::Graphics;

		FRHICommandList* Cmd = RHI->CreateCommandList(CmdType);
		if (Cmd == nullptr)
		{
			MAHO_CORE_ERROR("FRDGBuilder: failed to create command list for pass '{}'", Pass->GetName());
			continue;
		}

		Cmd->Begin();

		for (const auto& Barrier : CP.PreBarriers)
		{
			FRDGResource* Res = Barrier.first;
			ERHIResourceState Target = Barrier.second;
			if (auto* Buf = dynamic_cast<FRDGBuffer*>(Res))
			{
				if (auto* RHIBuf = Buf->GetRHI())
					Cmd->TransitionBuffer(RHIBuf, Res->CurrentState, Target);
			}
			else if (auto* Tex = dynamic_cast<FRDGTexture*>(Res))
			{
				if (auto* RHITex = Tex->GetRHI())
					Cmd->TransitionTexture(RHITex, Res->CurrentState, Target);
			}
		}

		// Auto BeginRendering/EndRendering for raster passes
		const FRDGPassParameters* Params = Pass->GetParameters();
		bool bBegunRendering = false;
		if (Pass->GetType() == ERDGPassType::Raster && Params != nullptr && !Params->RenderTargets.empty())
		{
			std::vector<FRHIRenderingAttachmentInfo> ColorAtts;
			FRHIRenderingAttachmentInfo DepthAtt;
			std::uint32_t Width = 0, Height = 0;
			BuildRenderingAttachments(Params, ColorAtts, DepthAtt, Width, Height);

			if (!ColorAtts.empty())
			{
				Cmd->BeginRendering(
					ColorAtts.data(), static_cast<std::uint32_t>(ColorAtts.size()),
					DepthAtt.View != nullptr ? &DepthAtt : nullptr,
					Width, Height);
				bBegunRendering = true;
			}
		}

		Pass->GetExecute()(*Cmd);

		if (bBegunRendering)
		{
			Cmd->EndRendering();
		}

		Cmd->End();

		// Cross-queue ordering: when the compute queue falls back to the graphics
		// family, submit compute passes on the graphics queue so they serialize
		// with raster passes (indirect draw reads compute-written buffers).
		const bool bComputeOnGraphics = RHI->GetComputeQueue().IsNativeFallback();
		if (Pass->GetType() == ERDGPassType::Compute && !bComputeOnGraphics)
			RHI->GetComputeQueue().Submit(&Cmd, 1, nullptr, 0, nullptr, 0, nullptr);
		else
			RHI->GetGraphicsQueue().Submit(&Cmd, 1, nullptr, 0, nullptr, 0, nullptr);

		RHI->DestroyCommandList(Cmd);
	}

	// Apply final transitions (external resources back to initial state).
	if (!FinalTransitions.empty())
	{
		FRHICommandList* Cmd = RHI->CreateCommandList(ERHICommandListType::Graphics);
		if (Cmd)
		{
			Cmd->Begin();
			for (const auto& [Res, OldState, NewState] : FinalTransitions)
			{
				if (auto* Buf = dynamic_cast<FRDGBuffer*>(Res))
				{
					if (auto* RHIBuf = Buf->GetRHI())
						Cmd->TransitionBuffer(RHIBuf, OldState, NewState);
				}
				else if (auto* Tex = dynamic_cast<FRDGTexture*>(Res))
				{
					if (auto* RHITex = Tex->GetRHI())
						Cmd->TransitionTexture(RHITex, OldState, NewState);
				}
			}
			Cmd->End();
			RHI->GetGraphicsQueue().Submit(&Cmd, 1, nullptr, 0, nullptr, 0, nullptr);
			RHI->DestroyCommandList(Cmd);
		}
	}

	// Upload staging buffers are one-shot — destroy after all copies are submitted.
	for (FRHIBuffer* Staging : UploadStagingBuffers)
	{
		RHI->DestroyBuffer(Staging);
	}
	UploadStagingBuffers.clear();
}

} // namespace Maho
