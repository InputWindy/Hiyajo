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
};

} // namespace Maho
