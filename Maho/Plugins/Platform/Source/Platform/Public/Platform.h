#pragma once

#include <Core/Misc/Delegate.h>
#include "PlatformApi.h"
#include <Core/Engine/EngineExtension.h>
#include "PlatformWindow.h"

#include <cstdint>

namespace Maho
{

MAHO_DECLARE_MULTICAST_DELEGATE(FOnRequestExit);

/**
 * Built-in platform window / headless clock extension.
 * Sole owner of the main FPlatformWindow. Shutdown order is auto-derived from
 * Render's Init dependency (Init mirror — shuts down after FRenderSystem,
 * whose TearDown needs the window).
 * Tick: PollEvents / ShouldClose / headless auto-exit.
 * Exit requests Broadcast OnRequestExit (FEngineBase binds in Init).
 */
class MAHO_PLATFORM_API FPlatformSystem final
	: public IEngineExtension
{
public:
	[[nodiscard]] FPlatformWindow* GetWindow() { return PlatformWindow.get(); }
	[[nodiscard]] const FPlatformWindow* GetWindow() const { return PlatformWindow.get(); }

	[[nodiscard]] FOnRequestExit& GetOnRequestExit() { return OnRequestExit; }
	[[nodiscard]] const FOnRequestExit& GetOnRequestExit() const { return OnRequestExit; }

	[[nodiscard]] bool IsHeadlessAutoExit() const { return bAutoExitAfterFrames; }
	[[nodiscard]] std::uint64_t GetAutoExitFrameCount() const { return AutoExitFrameCount; }

private:
	const char* GetName() const override { return "Platform"; }

	bool ExecuteStage(EEngineStage Stage) override;

	FPlatformWindowPtr PlatformWindow;
	FOnRequestExit OnRequestExit;
	FDelegateHandle AppRequestExitHandle;
	bool bAutoExitAfterFrames = false;
	std::uint64_t AutoExitFrameCount = 3;
};

} // namespace Maho
