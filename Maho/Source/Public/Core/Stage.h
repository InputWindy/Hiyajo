#pragma once

#include <cstdint>

namespace Maho
{

/** Toolkit lifecycle — a 2-value stage enum. */
enum class EToolStage : std::uint8_t
{
	Init = 0,
	Shutdown = 1,
};

/** Engine lifecycle stages. */
enum class EEngineStage : std::uint8_t
{
	PreInit = 0,
	Init,
	PostInit,
	PreTick,
	Tick,
	PostTick,
	PreShutdown,
	Shutdown,
	PostShutdown
};

} // namespace Maho
