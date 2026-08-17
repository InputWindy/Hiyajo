#pragma once

#include "ToolkitApi.h"
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

/** Pre-app toolkit: serial drive (no thread pool, no loop). */
class MAHO_TOOLKIT_API FToolkitBase
	: public ICommandLine
	, public TSerialScheduler<EToolStage>
{
protected:
	FToolkitBase() = default;

public:
	virtual ~FToolkitBase() = default;

protected:
	virtual void Init() = 0;
	virtual void Shutdown() = 0;
};

} // namespace Maho
