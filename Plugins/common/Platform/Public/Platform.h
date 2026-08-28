#pragma once

#include <Core/Interface.h>
#include <Core/Singleton.h>
#include <Maho.h>

#include <functional>
#include <memory>
#include <string_view>

// Win32 <Windows.h> #defines CreateWindow â†?CreateWindowW; keep the clean API name.
#ifdef CreateWindow
#	undef CreateWindow
#endif

namespace Maho
{
namespace Platform
{

/** Native surface handle â€?opaque (GLFWwindow*, EGLContext, ANativeWindow*, UIView*, ...). */
using FNativeSurface = void*;

/**
 * Minimal platform interface â€?only the native surface for the RHI.
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
 * Platform system â€?native surface + events (TSingleton service). Provides a
 * GLFW window (desktop) or a headless EGL pbuffer context. The host drives the
 * fixed lifecycle: Initialize() / Shutdown(); events are pumped explicitly via
 * PollEvents() (no implicit stage loop â€?the engine has none).
 *
 *   Platform::FPlatformSystem::Get().Initialize(0, nullptr);
 *   Platform::FPlatformSystem::Get().CreateWindow(1280, 720, "MyGame");
 *   while (!Platform::FPlatformSystem::Get().ShouldClose())
 *   {
 *       Platform::FPlatformSystem::Get().PollEvents();
 *       // render frame...
 *   }
 */
class FPlatformSystem
	: public TSingleton<FPlatformSystem>
	, public IPlugin<IInit, IShutdown>
{
public:
	/** Process-unique accessor â€?defined in Platform.cpp (in Platform.dll). */
	static FPlatformSystem& Get();

	void Initialize(int Argc, char** Argv) override;
	void Shutdown() override;

	/** Create a window (picks the backend for the current platform). */
	bool CreateWindow(int Width, int Height, std::string_view Title);

	/** Create a headless rendering context (EGL pbuffer on Linux). */
	bool CreateHeadlessContext(int Width, int Height);

	/** Destroy the backend (switch to headless). */
	void DestroyWindow();

	/** Pump platform events (call once per frame). */
	void PollEvents();

	[[nodiscard]] FNativeSurface GetNativeWindow() const;
	[[nodiscard]] bool IsHeadless() const { return Surface == nullptr; }

	/** Window close request (false when headless or no events). */
	[[nodiscard]] bool ShouldClose() const;

protected:
	friend TSingleton<FPlatformSystem>;
	FPlatformSystem() = default;

	std::unique_ptr<IPlatform> Surface;
	std::function<void()> PollEventsFn;
	std::function<bool()> QueryShouldClose;
};

} // namespace Platform
} // namespace Maho
