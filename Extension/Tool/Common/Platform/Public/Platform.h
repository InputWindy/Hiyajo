#pragma once

#include "PlatformApi.h"
#include <Engine/Tool.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>

// Win32 <Windows.h> #defines CreateWindow → CreateWindowW; keep the clean API name.
#ifdef CreateWindow
#	undef CreateWindow
#endif

namespace Maho
{

namespace Platform
{

/** Native surface handle — opaque (GLFWwindow*, EGLContext, ANativeWindow*, UIView*, ...). */
using FNativeSurface = void*;

/**
 * Minimal platform interface — only the native surface for the RHI.
 * Not every platform has a "window" (headless, Android surface, iOS view),
 * so window semantics are hidden behind this single accessor.
 */
class MAHO_PLATFORM_API IPlatform
{
public:
	virtual ~IPlatform() = default;

	/** Native window / surface handle; nullptr when headless or creation failed. */
	[[nodiscard]] virtual FNativeSurface GetNativeWindow() const = 0;
};

/**
 * Platform system — surface + events. A driven tool; the host (Engine/Layer)
 * specialises ExecuteExtension<FPlatformTool, HostStage> to pump events and
 * tear the window down at the right stages.
 *
 *   // read (const, public — any side may query)
 *   FPlatformTool::Get().GetNativeWindow();
 *   FPlatformTool::Get().ShouldClose();
 *
 *   // write (non-const, protected — scheduler only, via ExecuteExtension)
 *   //   CreateWindow / PollEvents / DestroyWindow
 *
 * Headless when no backend is created.
 */
class MAHO_PLATFORM_API FPlatformTool : public Maho::TTool<FPlatformTool>
{
public:
	/** Identity tag — this is a Tool. */
	using FTags = TTypeList<FToolTag>;

	// ── 读接口（const，public，无竞争）──

	/** Native surface for the RHI; nullptr when headless or creation failed. */
	[[nodiscard]] FNativeSurface GetNativeWindow() const;

	/** True when no backend surface exists. */
	[[nodiscard]] bool IsHeadless() const { return Surface == nullptr; }

	/** Window close request (false when headless or no events). */
	[[nodiscard]] bool ShouldClose() const;

protected:
	// ── 写接口（非 const，protected，仅调度器通过 ExecuteExtension 调用）──

	/** Create a window (picks the backend for the current platform). */
	bool CreateWindow(int Width, int Height, std::string_view Title);

	/** Create a headless rendering context (EGL pbuffer on Linux). */
	bool CreateHeadlessContext(int Width, int Height);

	/** Destroy the backend (switch to headless). */
	void DestroyWindow();

	/** Pump the event queue once (host Tick). */
	void PollEvents();

	// The ONLY external write entry — the scheduler's ExecuteExtension<T, Stage>.
	template <typename TExtension, typename TStage>
	friend bool Maho::ExecuteExtension(TStage Stage);

private:
	std::unique_ptr<IPlatform> Surface;
	std::function<void()> EventPump;
	std::function<bool()> CloseQuery;
};

} // namespace Platform

} // namespace Maho
