#pragma once

#include <Core/Export.h>

namespace Maho
{

class MAHO_API IInit
{
public:
	virtual ~IInit() = default;
	virtual void Initialize(int Argc, char** Argv) = 0;
};

class MAHO_API IShutdown
{
public:
	virtual ~IShutdown() = default;
	virtual void Shutdown() = 0;
};

/** Main capability — the layer's free-form run entry (a layer owns its loop). */
class MAHO_API IMain
{
public:
	virtual ~IMain() = default;
	virtual int Main() = 0;
};

/** Exit capability — request a running loop (IMain) to stop. */
class MAHO_API IExit
{
public:
	virtual ~IExit() = default;
	virtual void Exit() = 0;
};

/**
 * Capability composer — a virtual base that installs every capability trait as
 * a virtual base, so a contract = a list of stages/interfaces it promises.
 *
 *   class FRenderer : public FLayer<...>, public virtual IPlugin<IMain, IRenderer> {};
 */
template <typename... TCapabilities>
class MAHO_API IPlugin : public virtual TCapabilities...
{
public:
	virtual ~IPlugin() = default;
};

}