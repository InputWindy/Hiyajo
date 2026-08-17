#pragma once

namespace Maho
{

/** The running app — extensions request shutdown through it. */
class IAppContext
{
public:
	virtual ~IAppContext() = default;

	/** Request the app's main loop to exit (safe to call from any extension). */
	virtual void RequestShutdown() = 0;
};

/** The running app instance — set by FEngineBase's ctor, read by extensions (e.g. Platform). */
inline IAppContext* GApp = nullptr;

} // namespace Maho
