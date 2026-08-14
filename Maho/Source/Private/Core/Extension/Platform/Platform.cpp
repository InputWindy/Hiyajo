#include <Core/Extension/Platform/Platform.h>

#include <Core/App.h>
#include <Core/Misc/Console.h>
#include <Core/Misc/Log.h>

#include <algorithm>

namespace Maho
{

namespace
{

static TAutoConsoleVariable GCVarHeadlessAutoExitFrames(
	"app.Headless.AutoExitFrames",
	3,
	"Headless auto-exit after N frames (when Window.Create=0)");

} // namespace

bool FPlatformSystem::ExecuteStage(EEngineStage Stage)
{
	switch (Stage)
	{
	case EEngineStage::Init:
	{
		if (!GApp)
		{
			MAHO_CORE_ERROR("FPlatformSystem: GApp missing at Init");
			return false;
		}
		const FConfig& Config = GApp->GetConfig();
		FPlatformWindowDesc PlatformDesc;
		PlatformDesc.Platform = Config.Platform;
		PlatformDesc.Title = Config.ApplicationName.empty() ? "Maho" : Config.ApplicationName;
		PlatformDesc.Width = Config.WindowWidth;
		PlatformDesc.Height = Config.WindowHeight;
		PlatformDesc.bResizable = Config.bResizableWindow;
		PlatformDesc.bHeadless = !Config.bCreateMainWindow;

		PlatformWindow = FPlatformWindowFactory::Create(PlatformDesc);
		if (!PlatformWindow)
		{
			MAHO_CORE_ERROR("FPlatformSystem: failed to create platform window");
			return false;
		}

		if (PlatformDesc.bHeadless)
		{
			bAutoExitAfterFrames = true;
			AutoExitFrameCount = static_cast<std::uint64_t>(
				(std::max)(1, GCVarHeadlessAutoExitFrames.GetValue()));
			MAHO_CORE_INFO("Platform window headless; auto-exit after {} frames", AutoExitFrameCount);
		}

		AppRequestExitHandle = OnRequestExit.AddRaw(GApp, &FAppBase::OnRequestExit);
		return true;
	}
	case EEngineStage::Tick:
	{
		bool bShouldRequestExit = false;

		if (PlatformWindow)
		{
			PlatformWindow->PollEvents();
			if (PlatformWindow->ShouldClose())
			{
				bShouldRequestExit = true;
			}
		}

		if (bAutoExitAfterFrames && GApp && GApp->GetFrameIndex() >= AutoExitFrameCount)
		{
			bShouldRequestExit = true;
		}

		if (bShouldRequestExit)
		{
			OnRequestExit.Broadcast();
		}
		return true;
	}
	case EEngineStage::Shutdown:
		if (AppRequestExitHandle.IsValid())
		{
			OnRequestExit.Remove(AppRequestExitHandle);
			AppRequestExitHandle.Reset();
		}
		OnRequestExit.Clear();
		PlatformWindow.reset();
		FPlatformWindowFactory::Shutdown();
		return true;
	default:
		return true;
	}
}

} // namespace Maho
