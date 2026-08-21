#pragma once

#include <Core/Export.h>

namespace Maho
{

// ───────────────────────────────────────────────────────────────────────
// Interface.h — every pure interface in one place. These are contracts only
// (virtuals + no state); implementations are built by combining them with the
// concrete bases (TSingleton, FParallelScheduler, ...).
// ───────────────────────────────────────────────────────────────────────

/** Singleton identity — a class exposing `static T::Get()` (CRTP singleton). */
class ISingleton
{
public:
	virtual ~ISingleton() = default;
};

/** Runnable contract — anything with a Main entry. */
class IRunable
{
public:
	virtual ~IRunable() = default;

	virtual int Main(int Argc, char** Argv) = 0;
};

/**
 * Layer contract — the installable application root instance. A runnable that
 * is also dynamically installable: its DLL exports `CreateExtension` returning
 * an ILayer*. May be instantiated many times (NOT a singleton).
 */
class ILayer : public virtual IRunable
{
public:
	virtual ~ILayer() = default;

	virtual void Initialize(int Argc, char** Argv) = 0;
	virtual void Shutdown() = 0;
};

/**
 * Tool contract — a plug-and-play service instance. No Main; lifecycle is
 * OPTIONAL (default no-op, a Tool overrides only when it owns resources).
 * Tools are also instantiable (owned in a host's std::vector<ITool*>) and
 * driven like Layers — but their public capability methods stay callable
 * directly (the plug-in-and-play property is about the API, not the storage).
 *
 *   class FLog : public Maho::ITool
 *   {
 *   public:
 *       void Log(const char*) const;
 *       void Initialize() override;   // only if it owns something
 *   };
 */
class ITool
{
public:
	virtual ~ITool() = default;

	virtual void Initialize() {}
	virtual void Shutdown() {}
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
 * State-free: only Run (drive callables) + Execute (drive extensions) are
 * declared here, as pure contracts.
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
