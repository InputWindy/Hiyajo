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
 * Platform system — surface + events. A Tool: plug-in-and-play, self-managed.
 * Read and write are all public; the host decides when to create the window,
 * pump events, and tear it down. No scheduler-ownership friend.
 *
 *   FPlatformTool::Get().CreateWindow(1280, 720, "MyGame");
 *   FPlatformTool::Get().PollEvents();
 *   FPlatformTool::Get().GetNativeWindow();
 *
 * Headless when no backend is created.
 */
class MAHO_PLATFORM_API FPlatformTool : public Maho::TTool<FPlatformTool>
{
public:
	// ── 读接口（public）──

	/** Native surface for the RHI; nullptr when headless or creation failed. */
	[[nodiscard]] FNativeSurface GetNativeWindow() const;

	/** True when no backend surface exists. */
	[[nodiscard]] bool IsHeadless() const { return Surface == nullptr; }

	/** Window close request (false when headless or no events). */
	[[nodiscard]] bool ShouldClose() const;

	// ── 写接口（public——Tool 自带管理）──

	/** Create a window (picks the backend for the current platform). */
	bool CreateWindow(int Width, int Height, std::string_view Title);

	/** Create a headless rendering context (EGL pbuffer on Linux). */
	bool CreateHeadlessContext(int Width, int Height);

	/** Destroy the backend (switch to headless). */
	void DestroyWindow();

	/** Pump the event queue once (host Tick). */
	void PollEvents();

private:
	std::unique_ptr<IPlatform> Surface;
	std::function<void()> EventPump;
	std::function<bool()> CloseQuery;
};

} // namespace Platform

} // namespace Maho
