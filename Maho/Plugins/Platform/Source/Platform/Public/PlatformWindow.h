#pragma once

#include "PlatformApi.h"
#include <Core/Engine/Engine.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Maho
{

struct FPlatformWindowDesc
{
	EPlatform Platform = EPlatform::Glfw;

	std::string Title = "Maho";
	int Width = 1280;
	int Height = 720;
	bool bResizable = true;

	/** If true: init platform runtime only (clock / PollEvents), no OS window. */
	bool bHeadless = false;
};

/**
 * Platform runtime + optional OS window (no third-party types in the public API).
 * Concrete backends (e.g. GLFW) are created through FPlatformWindowFactory.
 * Keyboard / mouse polling is handled by Dear ImGui (ImGuiIO / ImGui::IsKey*).
 *
 * Example:
 * ```
 *   Maho::FPlatformWindowDesc Desc;
 *   Desc.Title = "MyGame";
 *   Desc.Width = 1280;
 *   Desc.Height = 720;
 *   Desc.Platform = Maho::EPlatform::Glfw;
 *
 *   Maho::FPlatformWindowPtr Window = Maho::FPlatformWindowFactory::Create(Desc);
 *   while (Window && !Window->ShouldClose())
 *   {
 *       Window->PollEvents();
 *       // ...
 *   }
 *   Window.reset();
 *   Maho::FPlatformWindowFactory::Shutdown();
 * ```
 */
class MAHO_PLATFORM_API FPlatformWindow
{
public:
	virtual ~FPlatformWindow() = default;

	FPlatformWindow(const FPlatformWindow&) = delete;
	FPlatformWindow& operator=(const FPlatformWindow&) = delete;

	virtual void Destroy() = 0;

	/** True when the backend runtime is alive (headless or with a window). */
	[[nodiscard]] virtual bool IsValid() const = 0;

	/** True when an OS window exists. */
	[[nodiscard]] virtual bool HasOsWindow() const = 0;
	[[nodiscard]] virtual bool ShouldClose() const = 0;

	virtual void SetTitle(const std::string& Title) = 0;
	virtual void GetFramebufferSize(int& OutWidth, int& OutHeight) const = 0;

	/**
	 * Platform-native handle for WSI (HWND on Win32).
	 * Returns nullptr when unavailable / headless.
	 */
	[[nodiscard]] virtual void* GetNativeHandle() const { return nullptr; }

	/**
	 * Toolkit window handle for backends (GLFWwindow* when EPlatform::Glfw).
	 * Returns nullptr when unavailable / headless.
	 */
	[[nodiscard]] virtual void* GetToolkitWindowHandle() const { return nullptr; }

	/** Drain OS event queue. */
	virtual void PollEvents() = 0;

	/** Seconds since backend runtime init. */
	[[nodiscard]] virtual double GetTimeSeconds() const = 0;

	/**
	 * Drain OS file-drop paths queued since the last call (e.g. GLFW drop callback).
	 * Empty by default for backends that do not support drops.
	 */
	virtual void DrainDroppedFilePaths(std::vector<std::string>& OutPaths)
	{
		OutPaths.clear();
	}

protected:
	FPlatformWindow() = default;
};

/**
 * Deleter that frees the object inside Maho.dll (safe across EXE/DLL heaps).
 */
struct MAHO_PLATFORM_API FPlatformWindowDeleter
{
	void operator()(FPlatformWindow* Window) const;
};

using FPlatformWindowPtr = std::unique_ptr<FPlatformWindow, FPlatformWindowDeleter>;

/** Creates the platform-window backend selected by FPlatformWindowDesc::Platform. */
class MAHO_PLATFORM_API FPlatformWindowFactory
{
public:
	FPlatformWindowFactory() = delete;

	/**
	 * Initializes the backend runtime and optionally creates an OS window.
	 * Returns empty FPlatformWindowPtr on failure.
	 */
	[[nodiscard]] static FPlatformWindowPtr Create(const FPlatformWindowDesc& Desc);

	/**
	 * Tear down the process-wide backend runtime (e.g. glfwTerminate).
	 * Call after all FPlatformWindow instances are destroyed.
	 */
	static void Shutdown();
};

} // namespace Maho
