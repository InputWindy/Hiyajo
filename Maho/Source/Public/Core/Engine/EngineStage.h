#pragma once

#include <cstdint>

namespace Maho
{

/**
 * Unified lifecycle + coarse per-frame stages for IEngineExtension.
 * Boot: PreInit → Init → PostInit → Attach.
 * Tick: BeginFrame → Tick → EndFrame → PreRender → Render → PostRender.
 * Exit / unmount: PrepareExit → Detach → Shutdown.
 *
 * Game-world sub-stages (ProcessInput / FixedUpdate / Update / LateUpdate)
 * are not engine stages — they are ISystem hooks driven by FSystemGroup::Tick.
 */
enum class EEngineStage : std::uint8_t
{
	PreInit = 0,
	Init,
	PostInit,
	Attach,
	BeginFrame,
	Tick,
	EndFrame,
	PreRender,
	Render,
	PostRender,
	Detach,
	PrepareExit,
	Shutdown,
	COUNT
};

enum class EStageRepeatPolicy : std::uint8_t
{
	Once = 0,
	AccumulatedFixed
};

} // namespace Maho
