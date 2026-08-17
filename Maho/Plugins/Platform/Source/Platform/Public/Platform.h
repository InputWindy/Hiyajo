#pragma once

#include "PlatformApi.h"
#include <Engine.h>

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
 * Platform system — surface + events. Engine extension (driven by EEngineStage).
 *
 *   FPlatformSystem::Get().CreateWindow(1280, 720, "MyGame");    // windowed
 *   FPlatformSystem::Get().CreateHeadlessContext(1280, 720);     // headless (EGL)
 *
 * Headless when no backend is created. ExecuteStage(Tick) pumps events.
 */
class MAHO_PLATFORM_API FPlatformSystem final : public TExtension<EEngineStage, FPlatformSystem>
{
public:
	[[nodiscard]] bool ExecuteStage(EEngineStage Stage) override;

	/** Create a window (picks the backend for the current platform). */
	bool CreateWindow(int Width, int Height, std::string_view Title);

	/** Create a headless rendering context (EGL pbuffer on Linux). */
	bool CreateHeadlessContext(int Width, int Height);

	/** Destroy the backend (switch to headless). */
	void DestroyWindow();

	[[nodiscard]] FNativeSurface GetNativeWindow() const;
	[[nodiscard]] bool IsHeadless() const { return Surface == nullptr; }

	/** Window close request (false when headless or no events). */
	[[nodiscard]] bool ShouldClose() const;

private:
	friend TSingleton<FPlatformSystem>;
	FPlatformSystem() = default;

	std::unique_ptr<IPlatform> Surface;
	std::function<void()> PollEvents;
	std::function<bool()> QueryShouldClose;
};

} // namespace Platform

} // namespace Maho
