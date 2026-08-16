#pragma once

#include <Core/Core.h>

#include <cstdint>

namespace Maho
{

/** Toolkit lifecycle — a 2-value stage enum. */
enum class EToolStage : std::uint8_t
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

/** Pre-app toolkit: serial drive (no thread pool, no loop). */
class FToolkitBase : 
	public ICommandLine,
	public TSerialScheduler<EToolStage>
{
protected:
	FToolkitBase() = default;

public:
	virtual ~FToolkitBase() = default;

protected:
	virtual void Init() = 0;
	virtual void Shutdown() = 0;
};

/** Engine base: parallel drive (owns its thread pool). */
class FEngineBase;

/** The running engine instance — set by FEngineBase's ctor, read by extensions (e.g. Platform) to request exit. */
inline FEngineBase* GApp = nullptr;

class FEngineBase :
	public TParallelScheduler<EEngineStage>,
	public IRunable
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
	void RequestShutdown()
	{
		CurrentStage = EEngineStage::PreShutdown;
	}

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
