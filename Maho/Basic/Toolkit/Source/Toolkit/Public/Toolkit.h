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

/** Pre-app toolkit: parallel drive (owns its thread pool). */
class MAHO_TOOLKIT_API FToolkitBase
	: public ICommandLine
	, public TParallelScheduler<EToolStage>
	, public IExtension<EToolStage>
{
protected:
	FToolkitBase() = default;

public:
	virtual ~FToolkitBase() = default;
};

} // namespace Maho
