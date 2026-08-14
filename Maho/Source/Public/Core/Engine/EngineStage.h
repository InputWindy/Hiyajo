#pragma once

#include <cstdint>

namespace Maho
{

/**
 * Unified lifecycle stages for IEngineExtension.
 * Boot: PreInit → Init → PostInit → Attach.
 * Per-frame marker: Tick (FAppBase::Tick()).
 * Exit / unmount: PrepareExit → Detach → Shutdown.
 *
 * Game-world sub-stages (ProcessInput / FixedUpdate / Update / LateUpdate)
 * are not engine stages — they are ISystem hooks driven by FSystemGroup::Tick.
 * Render-world framing lives on FRenderSystem::BeginFrame / RenderFrame.
 */
enum class EEngineStage : std::uint8_t
{
	PreInit = 0,
	Init,
	PostInit,
	Attach,
	Tick,
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
