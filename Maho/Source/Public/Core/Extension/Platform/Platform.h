#pragma once

#include <Core/Misc/Delegate.h>
#include <Core/Misc/DependsPack.h>
#include <Core/Misc/Export.h>
#include <Core/Engine/EngineExtension.h>
#include <Core/Extension/Platform/PlatformWindow.h>
#include <Core/Misc/TypeList.h>

#include <cstdint>

namespace Maho
{

class FRenderSystem;

MAHO_DECLARE_MULTICAST_DELEGATE(FOnRequestExit);

/**
 * Built-in platform window / headless clock extension.
 * Sole owner of the main FPlatformWindow. Shutdown after FRenderSystem (TearDown needs the window).
 * BeginFrame: PollEvents / ShouldClose / headless auto-exit (before FRenderSystem ImGui NewFrame).
 * Exit requests Broadcast OnRequestExit (FApp binds in Init).
 */
class MAHO_API FPlatformSystem final
	: public IEngineExtension
	, public TDependsPack<
		TDependsOn<EEngineStage::Shutdown, TTypeList<FRenderSystem>, EExtensionDepStrength::Weak>>
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
