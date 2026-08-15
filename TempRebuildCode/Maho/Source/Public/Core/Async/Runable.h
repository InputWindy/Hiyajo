#pragma once

namespace Maho
{

/** Runnable interface: anything the app loop can drive. */
class IRunable
{
public:
	virtual ~IRunable() = default;
	virtual void MainLoop() = 0;
};

} // namespace Maho
