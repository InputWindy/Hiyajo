#pragma once

namespace Maho
{

class ICommandLine
{
public:
	virtual ~ICommandLine() = default;
	virtual void ParseCommandLine(int Argc, char** Argv) = 0;
};

/** Runnable interface: anything the app loop can drive. */
class IRunable : public ICommandLine
{
public:
	virtual ~IRunable() = default;
	virtual void MainLoop() = 0;

	/** Request the main loop to exit (only runnables have something to stop). */
	virtual void RequestShutdown() = 0;
};

/** The running app — set by FEngineBase's ctor, read by extensions (e.g. Platform). */
inline IRunable* GApp = nullptr;

} // namespace Maho
