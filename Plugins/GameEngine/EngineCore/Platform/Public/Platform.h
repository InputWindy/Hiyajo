#pragma once

#include <Core/Interface.h>
#include <Engine/Engine.h>
#include <Maho.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

// GLFW is an opaque toolkit handle -- forward-declared so the public header
// needs no glfw.h (the backend keeps the real pointer; ImGui's glfw backend
// installs its input callbacks on it).
struct GLFWwindow;

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
 * The complete set of OS input events GLFW can deliver during glfwPollEvents. EVERY
 * input the platform can produce is forwarded -- no curated subset is dropped -- so a
 * consumer (the InputEvent broker, an ImGui driver, ...) sees the full raw input
 * stream and decides which of it to use. Engine-core knows no ImGui/GLFW type; the
 * GLFW key/button/mod/action codes are stable, platform-agnostic ints.
 */
enum class MInputEventType : std::uint8_t
{
	None       = 0,
	Key,          // keyboard key     -> Key, Scancode, Action, Mods
	Char,         // Unicode text     -> Codepoint
	MouseMove,    // cursor position  -> X, Y
	MouseButton,  // mouse button     -> Key (GLFW button index), Action, Mods
	Scroll,       // wheel            -> X, Y (delta)
	CursorEnter,  // cursor in/out    -> Bool (entered)
	WindowFocus,  // window focus     -> Bool (focused)
};

/** A single raw input event, exact payload of its GLFW callback. Union-free flat
 *  record: the fields that are not meaningful for a given Type are left at default
 *  (the doc above maps each Type to the fields it fills). */
struct MInputEvent
{
	MInputEventType Type = MInputEventType::None;

	std::int32_t  Key      = 0;   // GLFW key code (Key) / GLFW button index (MouseButton)
	std::int32_t  Scancode = 0;   // hardware scancode (Key)
	std::uint8_t  Action   = 0;   // GLFW_PRESS/RELEASE/REPEAT or mouse-button action
	std::uint32_t Codepoint = 0;  // Unicode code point (Char)
	std::uint16_t Mods     = 0;   // GLFW_MOD_* bitmask
	float         X = 0.f;        // cursor pos (MouseMove) / scroll delta (Scroll)
	float         Y = 0.f;        // cursor pos (MouseMove) / scroll delta (Scroll)
	bool          Bool = false;  // CursorEnter / WindowFocus
};

/**
 * Neutral input snapshot shared between Platform (writer: its GLFW callbacks fire on
 * the window-loop thread during PollEvents) and the UI features (readers: render thread).
 * Engine-core knows no ImGui/GLFW type:
 *   - mouse pos/buttons, keyboard down-state, mods, cursor-in and window-focus are
 *     pull-current and a REPEATABLE snapshot (any reader gets the same value; nothing
 *     is consumed).
 *   - the wheel is INCREMENTAL (it has no persistent "position" to store as a snapshot)
 *     so it is accumulated and exchanged on read -- single consumer (the editor scrolls
 *     the Console; the game UI does not scroll).
 *   - keyboard is indexed by GLFW key code (a stable, platform-agnostic int the UI
 *     layer maps to ImGuiKey_); its down-state is a repeatable snapshot.
 * Edges/char/focus are NOT in this snapshot -- they are transient, so FPlatform
 * exposes them as a drainable event stream (DrainInputEvents).
 */
struct MInputContext
{
	static constexpr int KeyCount = 512;   // comfortably past GLFW_KEY_LAST

	float MouseX	= 0.0f;
	float MouseY	= 0.0f;
	bool  MouseButtons[3] = { false, false, false };
	float MouseWheelX	= 0.0f;            // accumulated since last read (exchanged)
	float MouseWheelY	= 0.0f;            // accumulated since last read (exchanged)
	// Keyboard down-state, indexed by GLFW key code (GLFW_KEY_*). Index 0..KeyCount-1.
	bool  KeyDown[KeyCount] = {};
	std::uint16_t Mods	= 0;            // GLFW_MOD_* current (pushed by key/button cb)
	bool  MouseEntered	= false;        // cursor inside the window content area
	bool  WindowFocused	= false;        // window has input focus
};

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
 *   Engine.Install<FPlatform>();   // or Install(ApplyModuleExtension("FPlatform"))
 */
class FPlatform : public FLayer<IPreInit, IInit, IPostInit, IBeginFrame, ITick, IEndFrame, IExit, IPreShutdown, IShutdown, IPostShutdown>
{
public:
	MAHO_DECLARE_LAYER(FPlatform);

	FPlatform();
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

	/** Created window size (from DefaultEngine.ini). */
	[[nodiscard]] std::uint32_t GetWindowWidth() const { return WindowWidth; }
	[[nodiscard]] std::uint32_t GetWindowHeight() const { return WindowHeight; }

	/** The raw toolkit window handle (GLFWwindow*) -- for ImGui's glfw backend,
	 *  which installs its input callbacks on it. Null when headless. This is a
	 *  narrow bridge (like the RHI's ImGui bridge); GetNativeWindow() (HWND)
	 *  remains what the RHI uses. */
	[[nodiscard]] GLFWwindow* GetToolkitWindowHandle() const
	{
		return GlfwWindowFn ? GlfwWindowFn() : nullptr;
	}

	/** Copy the current input snapshot (mouse pos/buttons + keyboard down-state + mods
	 *  + cursor-in + focus) into Out. Any reader may call this every frame; it is a
	 *  REPEATABLE pull-current snapshot (nothing is consumed). The wheel fields of the
	 *  snapshot are NOT a snapshot -- use ConsumeMouseWheelXY() for the single-consumer
	 *  delta. */
	void ReadInput(MInputContext& Out) const;

	/** Drain the full raw input event stream accumulated since the last call (edges,
	 *  char, cursor/button/key/scroll events, cursor-enter, window-focus). Appends to
	 *  Out and clears the platform buffer. This is the INCREMENTAL counterpart to
	 *  ReadInput (which is the repeatable state snapshot). */
	void DrainInputEvents(std::vector<MInputEvent>& Out) const;

	/** Drain the file paths dropped onto the window since the last call (OS drag &
	 *  drop from a file manager). Appends to Out and clears the platform buffer.
	 *  Paths are PHYSICAL ABSOLUTE paths in the engine's narrow-char convention --
	 *  the platform does no virtual-path mapping (FPaths resolves that upward). */
	void DrainDroppedFiles(std::vector<std::string>& Out) const;

	/** Consume the accumulated mouse-wheel scroll deltas (GLFW notch units) since the
	 *  last call. Single consumer (the editor); exchange-to-zero. */
	void ConsumeMouseWheelXY(float& OutX, float& OutY);

	/** Consume the accumulated mouse-wheel scroll delta (Y, GLFW notch units) since the
	 *  last call. Single consumer (the editor); exchange-to-zero. Returns 0 when nothing
	 *  scrolled since last call. */
	[[nodiscard]] float ConsumeMouseWheelY();

private:
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
	std::function<GLFWwindow*()> GlfwWindowFn;

	// -- input snapshot written by the GLFW callbacks (window-loop thread) and read by
	//    the UI features (render thread). Protected by InputMutex.
	mutable std::mutex InputMutex;
	MInputContext Input;
	mutable std::vector<MInputEvent> InputEvents;
	// Dropped file paths (OS drag & drop), same producer/consumer split as InputEvents.
	mutable std::vector<std::string> DroppedFiles;

	std::uint32_t WindowWidth = 0;
	std::uint32_t WindowHeight = 0;
};

} // namespace Platform
} // namespace Maho
