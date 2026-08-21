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

/** Extension identity — a declared service/plugin (carries a FDependsPack). */
class IExtension
{
public:
	virtual ~IExtension() = default;
};

} // namespace Maho
