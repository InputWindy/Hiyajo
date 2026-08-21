#pragma once

#include <Core/Export.h>

namespace Maho
{

// ───────────────────────────────────────────────────────────────────────
// Interface.h — capabilities composed into contracts via IPlugin.
//
// A capability is a single unit of behaviour (one stage, one role), declared
// as an empty trait. IPlugin<Caps...> owns them as virtual bases — a contract
// that lists which capabilities the object promises. This is interface
// composition at the same granularity as the lifecycle stages.
//
//   class ILayer : public virtual IPlugin<IInit, ITick, IShutdown, IMainLoop> {};
//
// An object only implements the capabilities it declares; others are a no-op.
// ───────────────────────────────────────────────────────────────────────

// ── capability traits ──

/** Main loop capability — anything that owns a run loop. */
class IMainLoop
{
public:
	virtual ~IMainLoop() = default;
	virtual int MainLoop(int Argc, char** Argv) = 0;
};

/** Initialize capability (one-shot setup). */
class IInit
{
public:
	virtual ~IInit() = default;
	virtual void Initialize(int Argc, char** Argv) = 0;
};

/** Tick capability (per-frame step). */
class ITick
{
public:
	virtual ~ITick() = default;
	virtual void Tick() = 0;
};

/** Shutdown capability. */
class IShutdown
{
public:
	virtual ~IShutdown() = default;
	virtual void Shutdown() = 0;
};

/**
 * Capability composer — a virtual base that installs every capability trait as
 * a virtual base, so a contract = a list of stages it promises.
 *
 *   using FBasicLayers = IPlugin<IInit, ITick, IShutdown>;
 *   class ILayer      : public virtual IPlugin<IInit, IShutdown, IMainLoop> {};
 */
template <typename... TCapabilities>
class IPlugin : public virtual TCapabilities...
{
public:
	virtual ~IPlugin() = default;
};

// ── contracts (compositions of the capabilities) ──

/** Runnable contract — anything with a Main (main-loop) entry. */
using IRunable = IPlugin<IMainLoop>;

/**
 * Layer contract — the installable application root instance. Lifecycle
 * (Init/Tick/Shutdown) + a main loop; owned by a host and driven by the
 * scheduler. May be instantiated many times.
 */
using ILayer = IPlugin<IInit, ITick, IShutdown, IMainLoop>;

/**
 * Tool contract — a plug-and-play service instance. Lifecycle stages but no
 * main loop; drivable like a Layer (owned in a host's vector) while its public
 * capability methods stay callable directly. Only the stages it declares are
 * exercised.
 */
using ITool = IPlugin<IInit, ITick, IShutdown>;

// ── standalone identities ──

/** Singleton identity — a class exposing `static T::Get()` (CRTP singleton). */
class ISingleton
{
public:
	virtual ~ISingleton() = default;
};

/** Extension identity — a declared service/plugin (carries a FDependsPack). */
class IExtension
{
public:
	virtual ~IExtension() = default;
};

/**
 * Scheduler contract — declares the traverse API; the derived policy
 * (FParallelScheduler / FSerialScheduler) provides the implementation.
 * State-free: only Run (drive callables) + Execute are declared here.
 */
class IScheduler
{
public:
	virtual ~IScheduler() = default;

	/** Drive every callable (serial / parallel — derived policy). */
	template <typename... FCallables>
	void Run(FCallables&&... Callables) const = delete;

	/** (See the derived policy for the two Execute overloads.) */
};

} // namespace Maho
