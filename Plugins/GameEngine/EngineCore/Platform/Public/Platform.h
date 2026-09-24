#pragma once

#include <Core/Delegate.h>
#include <Core/Interface.h>
#include <Core/ThreadedServer.h>
#include <Engine/Engine.h>
#include <Maho.h>

#include <atomic>
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
 *     so it is accumulated per FRAME and published with that frame; every consumer of the
 *     frame reads the same delta (no exchange-to-zero, no single-consumer constraint).
 *   - keyboard is indexed by GLFW key code (a stable, platform-agnostic int the UI
 *     layer maps to ImGuiKey_); its down-state is a repeatable snapshot.
 * Edges/char/focus are NOT in this snapshot -- they are transient. Together with the snapshot they
 * are published once per frame as a TAGGED FInputFrame (index + snapshot + that pump's events), and
 * each consumer walks the ring with its own cursor: every frame exactly once, in order. That is
 * what lets two ImGui contexts share the input without stealing a key RELEASE from each other --
 * see FInputFrame below.
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

/** One PUBLISHED input frame: the frame's index, the immutable snapshot taken at the end of that
 *  pump, and the EDGE events (keys, characters, wheel, focus, buttons) that arrived during the same
 *  pump. Publishing per frame -- instead of handing the events to whoever drains first -- is what
 *  makes the frame TAG possible, and the tag is what makes several consumers correct at once: each
 *  keeps its own cursor and applies every frame exactly once, to the frame it belongs to, even
 *  though up to MAHO_FRAMES_IN_FLIGHT of them are being processed concurrently. */
struct FInputFrame
{
	std::uint64_t Index = 0;
	MInputContext Snapshot;
	std::vector<MInputEvent> Events;
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
class FPlatform : public FFrameExtension, public FThreadedServer, public IPipeline<IInit, IPostInit, ITick, IEndFrame, IExit, IShutdown>
{
public:
	MAHO_DECLARE_FRAME(FPlatform);

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
	/** "No window" is judged on the CACHED handle, not on the backend object: the backend is owned
	 *  by the platform thread and only it may touch it. */
	[[nodiscard]] bool IsHeadless() const
	{
		std::lock_guard<std::mutex> L(NativeMutex);
		return CachedSurface == nullptr;
	}

	/** Window close request (false when headless or no events). */
	[[nodiscard]] bool ShouldClose() const;

	/** Created window size. A CACHE of the window's state, refreshed on the platform thread once per
	 *  pump -- the size must never be queried through GLFW from here (see the class comment below). */
	[[nodiscard]] std::uint32_t GetWindowWidth() const { return CachedWidth.load(std::memory_order_relaxed); }
	[[nodiscard]] std::uint32_t GetWindowHeight() const { return CachedHeight.load(std::memory_order_relaxed); }

	/** The raw toolkit window handle (GLFWwindow*) -- for ImGui's glfw backend,
	 *  which installs its input callbacks on it. Null when headless. This is a
	 *  narrow bridge (like the RHI's ImGui bridge); GetNativeWindow() (HWND)
	 *  remains what the RHI uses. Served from the platform thread's cache, because
	 *  the handle itself may only be produced on that thread. */
	[[nodiscard]] GLFWwindow* GetToolkitWindowHandle() const
	{
		std::lock_guard<std::mutex> L(NativeMutex);
		return CachedToolkitWindow;
	}

	/** Copy the current input snapshot (mouse pos/buttons + keyboard down-state + mods
	 *  + cursor-in + focus) into Out. Any reader may call this every frame; it is a
	 *  REPEATABLE pull-current snapshot (nothing is consumed). Its wheel fields are whatever the
	 *  current pump accumulated -- a reader that wants a FRAME's input, wheel included, wants
	 *  ReadInputFrame / ReadFrameInput instead. */
	void ReadInput(MInputContext& Out) const;

	/** Copy the most recently PUBLISHED input frame (see the ring in the private section).
	 *  This is the one a reader in a DIFFERENT frame graph must use. The UI stages are children
	 *  of the render collector, so no dependency edge can ever order them against Tick() -- and
	 *  reading ReadInput() would race the pump's live working copy. A published slot is complete
	 *  and never mutated again, so a reader lagging by up to MAHO_FRAMES_IN_FLIGHT frames still
	 *  gets a coherent snapshot instead of a torn one. */
	void ReadFrameInput(MInputContext& Out) const;

	/** The LATEST published input-frame index (monotonic, bumped once per published frame). A
	 *  consumer ALIGNS on this: it reads every frame it has not read yet, in order, exactly once. */
	[[nodiscard]] std::uint64_t GetInputFrameIndex() const;

	/** Read one TAGGED input frame -- its snapshot AND the edge events of that same pump. False when
	 *  the index has already been evicted (the reader fell more than kInputRingSlots frames behind),
	 *  which a consumer must treat as "I skipped input", never as "no input happened". */
	[[nodiscard]] bool ReadInputFrame(std::uint64_t Index, FInputFrame& Out) const;

	/** Fired at the END of every pump, carrying the index of the frame just published. Subscribers
	 *  do NOT receive the payload: each one owns a CURSOR and reads the frames it owes itself (see
	 *  the ring in the private section). That is the whole point -- the old drain handed the batch to
	 *  whoever asked first, so a consumer that missed the frame carrying a key RELEASE never learned
	 *  the key came up, and ImGui kept it "down" and auto-repeated it (one Backspace erasing the whole
	 *  line, one arrow walking the cursor to the far left). */
	TMulticastEvent<void(std::uint64_t)> OnInputPublished;

	/** Drain the file paths dropped onto the window since the last call (OS drag &
	 *  drop from a file manager). Appends to Out and clears the platform buffer.
	 *  Paths are PHYSICAL ABSOLUTE paths in the engine's narrow-char convention --
	 *  the platform does no virtual-path mapping (FPaths resolves that upward). */
	void DrainDroppedFiles(std::vector<std::string>& Out) const;

private:
	// -- engine pipeline stages (scheduler-only) --
	void Initialize(FEngineBase& Engine, FEngineContext& Frame) override;
	void PostInitialize(FEngineBase&, FEngineContext&) override {}
	void Shutdown(FEngineBase& Engine, FEngineContext& Frame) override;
	void Tick(FEngineBase& Engine, FEngineContext& Frame) override;
	void EndFrame(FEngineBase& Engine, FEngineContext& Frame) override;
	void RequestExit(FEngineBase& Engine, FEngineContext& Frame) override;

	// -- the platform's OWN thread (FThreadedServer) ---------------------------------------------
	//
	// Everything that touches the window lives on this thread, because a Win32 window's message
	// queue BELONGS to the thread that created it: PeekMessage only ever sees the CALLING thread's
	// queue, and keys / characters / wheel exist ONLY as messages. Off this thread the frame loop may
	// read the cached state below and nothing else.
	//
	// (Measured before this change: the window was created by whichever pool worker happened to run
	// IInit, so the pump -- dispatched to a DIFFERENT worker every frame -- was asking the wrong queue
	// in 23 of every 24 frames. Keys therefore arrived at ~2 per second with 0.2-2s of queue delay,
	// in bursts: "a tap does nothing", "a run of characters appears at once", "one Backspace erases
	// the whole line". The mouse survived only because its position and buttons are also read by
	// GLOBAL queries -- which is exactly the asymmetry reported from the start.)
	[[nodiscard]] bool OnInitialize() override;
	void OnShutdown() override;
	[[nodiscard]] const char* GetThreadName() const override;

	/** Run Fn ON the platform thread and wait for it; inline when already there (a task must never
	 *  wait on itself). The ONLY way any other thread may touch GLFW or the window. */
	void RunOnPlatformThread(const std::function<void()>& Fn);

	/** Refresh the frame-side caches below from the live window -- ON the platform thread, after
	 *  each pump, so the reads other threads do are always of a coherent moment. */
	void PublishWindowState();

	// -- frame-side caches of the window state: written on the platform thread, read anywhere --
	std::atomic<std::uint32_t> CachedWidth{ 0 };
	std::atomic<std::uint32_t> CachedHeight{ 0 };
	std::atomic<bool> bCachedShouldClose{ false };
	mutable std::mutex NativeMutex;   // guards CachedSurface + CachedToolkitWindow
	FNativeSurface CachedSurface{};
	GLFWwindow* CachedToolkitWindow = nullptr;

	std::unique_ptr<IPlatform> Surface;
	std::function<void()> PollEventsFn;
	std::function<bool()> QueryShouldClose;
	std::function<GLFWwindow*()> GlfwWindowFn;

	// -- input: written by the GLFW callbacks (window-loop thread), published once per frame, read
	//    by the UI features (render thread). Protected by InputMutex.
	mutable std::mutex InputMutex;
	MInputContext Input;   // the pump's LIVE working copy (written by the GLFW callbacks)

	/** The events of the frame currently being pumped. They are MOVED into the ring slot at publish
	 *  time and never handed to a consumer directly -- that is what stops one consumer from stealing
	 *  another's frames (the old drain could, and a stolen key RELEASE is what left ImGui believing
	 *  the key was still down). */
	std::vector<MInputEvent> FrameEvents;

	/** Published input ring: one slot per in-flight frame PLUS margin. The margin exists because
	 *  consumers read by INDEX now: a reader may be a couple of frames behind (a stage of a child
	 *  collector runs in its own frame graph), and the ring must still hold the frame it owes itself.
	 *  A consumer that falls further than this SKIPS frames -- and gets told, because silently
	 *  applying an older frame's input is worse than dropping it. */
	static constexpr std::uint32_t kInputRingSlots = MAHO_FRAMES_IN_FLIGHT + 4;
	FInputFrame InputRing[kInputRingSlots];
	std::uint32_t InputRingWrite = 0;
	std::uint64_t InputFrameCounter = 0;                      // writer-owned
	std::atomic<std::uint64_t> InputFramePublished{ 0 };      // what readers align on

	// Dropped file paths (OS drag & drop), same producer/consumer split.
	mutable std::vector<std::string> DroppedFiles;
};

} // namespace Platform
} // namespace Maho
