#pragma once

#include "EngineApi.h"
#include <Core/Core.h>

#include <cstdint>

namespace Maho
{

/** Engine lifecycle stages. */
enum class EEngineStage : std::uint8_t
{
	PreInit = 0,
	Init,
	PostInit,
	PreTick,
	Tick,
	PostTick,
	PreShutdown,
	Shutdown,
	PostShutdown
};

/** Engine base: parallel drive (owns its thread pool). */
class MAHO_ENGINE_API FEngineBase
	: public TParallelScheduler<EEngineStage>
	, public IRunable
	, public IExtension<EEngineStage>
{
protected:
	FEngineBase()
	{
		GApp = this;
	}

public:
	virtual ~FEngineBase() = default;

	/** Request the main loop to exit (safe to call from any extension). */
	void RequestShutdown() override
	{
		CurrentStage = EEngineStage::PreShutdown;
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
