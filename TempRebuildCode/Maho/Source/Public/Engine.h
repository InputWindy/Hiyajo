#pragma once

#include <Core/Core.h>

#include <cstdint>

namespace Maho
{

/** Singleton lifecycle — modeled as a 2-value stage enum. */
enum class ESingletonStage : std::uint8_t
{
	Init = 0,
	Shutdown = 1,
};

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

/** Pre-app singleton registry: serial drive (no thread pool). */
class FSingletonRegistryBase : 
	public ICommandLine,
	public TSerialScheduler<ESingletonStage>
{
protected:
	FSingletonRegistryBase() = default;

public:
	virtual ~FSingletonRegistryBase() = default;

protected:
	virtual void Init() = 0;
	virtual void Shutdown() = 0;
};

/** Engine base: parallel drive (owns its thread pool). */
class FEngineBase :
	public TParallelScheduler<EEngineStage>,
	public IRunable
{
protected:
	FEngineBase() = default;

	virtual void PreInit() = 0;
	virtual void Init() = 0;
	virtual void PostInit() = 0;
	virtual void PreTick() = 0;
	virtual void Tick() = 0;
	virtual void PostTick() = 0;
	virtual void PreShutdown() = 0;
	virtual void Shutdown() = 0;
	virtual void PostShutdown() = 0;

	/** Requested by an extension during Tick; exits the main loop. */
	void RequestShutdown()
	{
		CurrentStage = EEngineStage::PreShutdown;
	}

public:
	virtual ~FEngineBase() = default;

	void MainLoop() final override
	{
		PreInit();
		Init();
		PostInit();

		while (true)
		{
			PreTick();
			Tick();
			PostTick();

			if (CurrentStage == EEngineStage::PreShutdown)
			{
				break;
			}
		}

		PreShutdown();
		Shutdown();
		PostShutdown();
	}

private:
	EEngineStage CurrentStage = EEngineStage::PreInit;
};

} // namespace Maho
