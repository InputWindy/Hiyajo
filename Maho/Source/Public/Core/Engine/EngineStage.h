#pragma once

#include <cstdint>

namespace Maho
{

/**
 * Unified lifecycle + per-frame stages for IEngineExtension.
 * Boot: PreInit → Init → PostInit → Attach.
 * Tick: BeginFrame … PostRender.
 * Exit / unmount: PrepareExit → Detach → Shutdown.
 */
enum class EEngineStage : std::uint8_t
{
	PreInit = 0,
	Init,
	PostInit,
	Attach,
	BeginFrame,
	ProcessInput,
	FixedUpdate,
	Update,
	LateUpdate,
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
