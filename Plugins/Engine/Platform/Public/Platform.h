#pragma once

#include <Core/Interface.h>
#include <Engine/Engine.h>
#include <Maho.h>

#include <functional>
#include <memory>
#include <string_view>

// Win32 <Windows.h> #defines CreateWindow to CreateWindowW; keep the clean API name.
#ifdef CreateWindow
#	undef CreateWindow
#endif

namespace Maho
{
namespace Platform
{

class FPlatform;

/** Global platform instance accessor - returns FPlatform* (cross-DLL via function, no bare variable export). */
MAHO_API FPlatform* GetPlatform();

/** Native surface handle - opaque (GLFWwindow*, EGLContext, ANativeWindow*, UIView*, ...). */
using FNativeSurface = void*;

/**
 * Minimal platform interface - only the native surface for the RHI.
 * Not every platform has a "window" (headless, Android surface, iOS view),
 * so window semantics are hidden behind this single accessor.
 */
class IPlatform
{
public:
	virtual ~IPlatform() = default;

	/** Native window / surface handle; nullptr when headless or creation failed. */
	[[nodiscard]] virtual FNativeSurface GetNativeWindow() const = 0;
};

/**
 * Platform system - native surface + events (an FEngineLayer feature). The
 * engine loop drives Tick() -> PollEvents + ShouldClose -> Engine.RequestExit().
 * The RHI reaches this instance through GetPlatform() and reads
 * GetNativeWindow() - no singleton needed.
 *
 *   Platform::FPlatform Platform;
 *   Platform.Initialize(0, nullptr);
 *   Platform.CreateWindow(1280, 720, "MyGame");
 *   Engine.Install(&Platform);
 */
class FPlatform : public FLayer<IPreInit, IInit, IPostInit, IBeginFrame, ITick, IEndFrame, IExit, IPreShutdown, IShutdown, IPostShutdown>
{
public:
	MAHO_DECLARE_LAYER(FPlatform, "Platform.dll");

	~FPlatform() override;

	/** Create a window (picks the backend for the current platform). */
	bool CreateWindow(int Width, int Height, std::string_view Title);

	/** Create a headless rendering context (EGL pbuffer on Linux). */
	bool CreateHeadlessContext(int Width, int Height);

	/** Destroy the backend (switch to headless). */
	void DestroyWindow();

	/** Pump platform events (called by Tick). */
	void PollEvents();

	[[nodiscard]] FNativeSurface GetNativeWindow() const;
	[[nodiscard]] bool IsHeadless() const { return Surface == nullptr; }

	/** Window close request (false when headless or no events). */
	[[nodiscard]] bool ShouldClose() const;

private:
	FPlatform() = default;

	// -- engine pipeline stages (scheduler-only) --
	void PreInitialize(FEngineBase&) override {}
	void Initialize(FEngineBase& Engine) override;
	void PostInitialize(FEngineBase&) override {}
	void Shutdown(FEngineBase& Engine) override;
	void BeginFrame(FEngineBase& Engine) override;
	void Tick(FEngineBase& Engine) override;
	void EndFrame(FEngineBase& Engine) override;
	void RequestExit(FEngineBase& Engine) override;
	void PreShutdown(FEngineBase&) override {}
	void PostShutdown(FEngineBase&) override {}

	std::unique_ptr<IPlatform> Surface;
	std::function<void()> PollEventsFn;
	std::function<bool()> QueryShouldClose;
};

} // namespace Platform
} // namespace Maho
