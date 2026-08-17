#pragma once

#include "EngineApi.h"
#include <Core/Core.h>

#include <cstdint>

namespace Maho
{

/** Engine base: parallel drive (owns its thread pool). */
class MAHO_ENGINE_API FEngineBase
	: public TParallelScheduler<EEngineStage>
	, public IRunable
	, public IExtension<EEngineStage>
	, public IAppContext
{
protected:
	FEngineBase()
	{
		GApp = this;
	}

	virtual void PreInit() = 0;
	virtual void Init() = 0;
	virtual void PostInit() = 0;
	virtual void PreTick() = 0;
	virtual void Tick() = 0;
	virtual void PostTick() = 0;
	virtual void PreShutdown() = 0;
	virtual void Shutdown() = 0;
	virtual void PostShutdown() = 0;

public:
	virtual ~FEngineBase() = default;

	/** Request the main loop to exit (safe to call from any extension). */
	void RequestShutdown() override
	{
		CurrentStage = EEngineStage::PreShutdown;
	}

	/** Unified stage drive: dispatches to the 9 stage virtuals. */
	bool ExecuteStage(EEngineStage Stage) override
	{
		switch (Stage)
		{
		case EEngineStage::PreInit: PreInit(); break;
		case EEngineStage::Init: Init(); break;
		case EEngineStage::PostInit: PostInit(); break;
		case EEngineStage::PreTick: PreTick(); break;
		case EEngineStage::Tick: Tick(); break;
		case EEngineStage::PostTick: PostTick(); break;
		case EEngineStage::PreShutdown: PreShutdown(); break;
		case EEngineStage::Shutdown: Shutdown(); break;
		case EEngineStage::PostShutdown: PostShutdown(); break;
		}
		return true;
	}

	/** Convenience: run the full lifecycle loop via ExecuteStage. */
	void MainLoop() final override
	{
		ExecuteStage(EEngineStage::PreInit);
		ExecuteStage(EEngineStage::Init);
		ExecuteStage(EEngineStage::PostInit);

		while (CurrentStage != EEngineStage::PreShutdown)
		{
			ExecuteStage(EEngineStage::PreTick);
			ExecuteStage(EEngineStage::Tick);
			ExecuteStage(EEngineStage::PostTick);
		}

		ExecuteStage(EEngineStage::PreShutdown);
		ExecuteStage(EEngineStage::Shutdown);
		ExecuteStage(EEngineStage::PostShutdown);
	}

private:
	EEngineStage CurrentStage = EEngineStage::PreInit;
};

} // namespace Maho
