#include "Platform.h"

#include <Config.h>
#include <ConsoleVariable.h>
#include <Log.h>
#include <Trace.h>

#include <chrono>
#include <cstdio>

#if !defined(MAHO_HEADLESS)
#	if defined(_WIN32)
#		define GLFW_EXPOSE_NATIVE_WIN32
#	endif
#	include <GLFW/glfw3.h>
#	if defined(_WIN32)
#		include <GLFW/glfw3native.h>
#	endif
#endif

#if defined(__linux__)
#	define EGL_EGLEXT_PROTOTYPES 1
#	include <EGL/egl.h>
#	include <EGL/eglext.h>
#endif

// GLFW pulls in <Windows.h>, which #defines CreateWindow -> CreateWindowW.
#ifdef CreateWindow
#	undef CreateWindow
#endif

#if defined(_WIN32)
#	include <windows.h>
#	include <imm.h>   // ImmAssociateContext: detach the IME from our window (see PollEvents)
#	pragma comment(lib, "imm32.lib")   // ... which lives in imm32, not in the default lib set
#endif

namespace Maho::Platform
{

namespace
{
/** GLFW hands drop paths as UTF-8; the engine's file APIs are all narrow-char
 *  (native ANSI code page on Windows). Convert at this boundary so a dropped path
 *  behaves like every other path producer (ExecutableDir, ini reads, ifstream) on
 *  the way down. On other platforms narrow IS UTF-8 and the copy stands. */
std::string ToNativeNarrowPath(const char* Utf8)
{
	if (Utf8 == nullptr)
	{
		return {};
	}
#if defined(_WIN32)
	const int WideLen = ::MultiByteToWideChar(CP_UTF8, 0, Utf8, -1, nullptr, 0);
	if (WideLen <= 1)
	{
		return {};
	}
	std::wstring Wide(static_cast<std::size_t>(WideLen), L'\0');
	::MultiByteToWideChar(CP_UTF8, 0, Utf8, -1, Wide.data(), WideLen);

	const int NarrowLen = ::WideCharToMultiByte(CP_ACP, 0, Wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
	if (NarrowLen <= 1)
	{
		return {};
	}
	std::string Narrow(static_cast<std::size_t>(NarrowLen), '\0');
	::WideCharToMultiByte(CP_ACP, 0, Wide.c_str(), -1, Narrow.data(), NarrowLen, nullptr, nullptr);
	Narrow.resize(static_cast<std::size_t>(NarrowLen - 1));   // drop the terminating NUL
	return Narrow;
#else
	return std::string(Utf8);
#endif
}
} // namespace

FPlatform* GPlatform = nullptr;

MAHO_API FPlatform* GetPlatform()
{
	return GPlatform;
}

FPlatform::~FPlatform() = default;

FPlatform::FPlatform()
{
	// The window size is read from the Config layer in Initialize - Config must
	// be initialized first.
	MyStage<IInit>().IsWaiting<Config::FConfig>().ForStage<IInit>();
	// Initialize logs "CreateWindow"; the Log layer must be initialized first.
	MyStage<IInit>().IsWaiting<FLog>().ForStage<IInit>();
	// Note: "the window/surface must outlive FRender" is declared by FRender
	// itself via BlockOn (it is the consumer of our window); we do not know it.
}

namespace
{
	static ConsoleVariable::TAutoConsoleVariable<int> GCVarWindowWidth(
		"r.Window.Width", 1280, "Platform window width", ConsoleVariable::ECVarFlags::Shipping);

	static ConsoleVariable::TAutoConsoleVariable<int> GCVarWindowHeight(
		"r.Window.Height", 720, "Platform window height", ConsoleVariable::ECVarFlags::Shipping);

	static ConsoleVariable::TAutoConsoleVariable<std::string> GCVarWindowTitle(
		"r.Window.Title", "Maho", "Platform window title", ConsoleVariable::ECVarFlags::Shipping);

#if !defined(MAHO_HEADLESS)
			// -- GLFW backend (desktop: Windows / Linux, windowed) --

	class FGlfwWindow final : public IPlatform
	{
	public:
		FGlfwWindow(int Width, int Height, std::string_view Title)
		{
			if (glfwInit())
			{
				Window = glfwCreateWindow(Width, Height, std::string(Title).c_str(), nullptr, nullptr);
				if (Window != nullptr)
				{
					// Push live framebuffer (pixel) size to the owner on every resize, so
					// the ImGui DisplaySize / window layout track the OS window. GLFW
					// callbacks fire during glfwPollEvents (the engine's Tick thread).
					glfwSetWindowUserPointer(Window, this);
					glfwSetFramebufferSizeCallback(Window, [](GLFWwindow* W, int FW, int FH)
					{
						auto* Self = static_cast<FGlfwWindow*>(glfwGetWindowUserPointer(W));
						if (Self != nullptr && Self->OnFramebufferSize)
						{
							Self->OnFramebufferSize(FW, FH);
						}
					});
					// Mouse wheel is a discrete event GLFW consumes during the pump, so a no-backend
					// ImGui context can't poll it via key state. Accumulate the delta here; it is
					// published with THIS frame, so every consumer of the frame reads the same value.
					glfwSetScrollCallback(Window, [](GLFWwindow* W, double XOff, double YOff)
					{
						auto* Self = static_cast<FGlfwWindow*>(glfwGetWindowUserPointer(W));
					if (Self != nullptr && Self->OnMouseWheel)
					{
						Self->OnMouseWheel(XOff, YOff);
					}
				});
						// Cursor / button / key / char / cursor-enter / focus: the OS event
						// source produces these in the window-loop thread (glfwPollEvents);
						// forward them to the owner so it can fold them into the shared
						// MInputContext state snapshot + the drainable MInputEvent stream,
						// passing the FULL callback payload (scancode + mods, etc.) -- the
						// consumer decides what it needs; nothing curated out here.
						glfwSetCursorPosCallback(Window, [](GLFWwindow* W, double X, double Y)
						{
							auto* Self = static_cast<FGlfwWindow*>(glfwGetWindowUserPointer(W));
							if (Self && Self->OnCursorPos) Self->OnCursorPos(X, Y);
						});
						glfwSetMouseButtonCallback(Window, [](GLFWwindow* W, int Button, int Action, int Mods)
						{
							auto* Self = static_cast<FGlfwWindow*>(glfwGetWindowUserPointer(W));
							if (Self && Self->OnMouseButton) Self->OnMouseButton(Button, Action, Mods);
						});
						glfwSetKeyCallback(Window, [](GLFWwindow* W, int Key, int Scancode, int Action, int Mods)
						{
							auto* Self = static_cast<FGlfwWindow*>(glfwGetWindowUserPointer(W));
							if (Self && Self->OnKey) Self->OnKey(Key, Scancode, Action, Mods);
						});
						glfwSetCharCallback(Window, [](GLFWwindow* W, unsigned int Codepoint)
						{
							auto* Self = static_cast<FGlfwWindow*>(glfwGetWindowUserPointer(W));
							if (Self && Self->OnChar) Self->OnChar(static_cast<std::uint32_t>(Codepoint));
						});
						glfwSetCursorEnterCallback(Window, [](GLFWwindow* W, int Entered)
						{
							auto* Self = static_cast<FGlfwWindow*>(glfwGetWindowUserPointer(W));
							if (Self && Self->OnCursorEnter) Self->OnCursorEnter(Entered != 0);
						});
						glfwSetWindowFocusCallback(Window, [](GLFWwindow* W, int Focused)
						{
							auto* Self = static_cast<FGlfwWindow*>(glfwGetWindowUserPointer(W));
							if (Self && Self->OnWindowFocus) Self->OnWindowFocus(Focused != 0);
						});
						// OS drag & drop from a file manager: paths are UTF-8 (GLFW's
						// contract), per-file forwarded to the owner which converts them to
						// the engine's narrow-char convention and queues them.
						glfwSetDropCallback(Window, [](GLFWwindow* W, int Count, const char** Paths)
						{
							auto* Self = static_cast<FGlfwWindow*>(glfwGetWindowUserPointer(W));
							if (Self && Self->OnDropFile)
							{
								for (int I = 0; I < Count; ++I)
								{
									Self->OnDropFile(Paths[I]);
								}
							}
						});
				}
			}
		}

		~FGlfwWindow() override
		{
			if (Window != nullptr)
			{
				glfwDestroyWindow(Window);
			}
			glfwTerminate();
		}

		[[nodiscard]] FNativeSurface GetNativeWindow() const override
		{
#if defined(_WIN32)
			// The RHI needs the real OS window handle (HWND), not the GLFWwindow*.
			return static_cast<FNativeSurface>(glfwGetWin32Window(Window));
#else
			return static_cast<FNativeSurface>(Window);
#endif
		}

		void PollEvents() { glfwPollEvents(); }
		[[nodiscard]] bool ShouldClose() const { return Window != nullptr && glfwWindowShouldClose(Window); }

		/** Raw GLFW window handle -- for ImGui's glfw backend (input callbacks). */
		[[nodiscard]] GLFWwindow* GetGlfwWindow() const { return Window; }

		/** Framebuffer-resize listener, invoked by the GLFW callback (owner wires it). */
		std::function<void(int, int)> OnFramebufferSize;

		/** Mouse-wheel listener, invoked by the GLFW scroll callback (owner wires it).
		 *  Args: (X delta, Y delta). */
		std::function<void(double, double)> OnMouseWheel;

		/** Cursor-position listener, invoked by the GLFW cursor callback (owner wires it). */
		std::function<void(double, double)> OnCursorPos;
		/** Mouse-button listener, invoked by the GLFW mouse-button callback (owner wires it).
		 *  Args: (GLFW mouse button, GLFW action, GLFW mods). */
		std::function<void(int, int, int)> OnMouseButton;
		/** Key listener, invoked by the GLFW key callback (owner wires it). Args:
		 *  (GLFW key code, GLFW scancode, GLFW action, GLFW mods). */
		std::function<void(int, int, int, int)> OnKey;
		/** Unicode character listener, invoked by the GLFW char callback (owner wires it).
		 *  Args: (Unicode code point). */
		std::function<void(std::uint32_t)> OnChar;
		/** Cursor-enter listener, invoked by the GLFW cursor-enter callback (owner wires it).
		 *  Args: (entered bool). */
		std::function<void(bool)> OnCursorEnter;
		/** Window-focus listener, invoked by the GLFW window-focus callback (owner wires it).
		 *  Args: (focused bool). */
		std::function<void(bool)> OnWindowFocus;

		/** Dropped-file listener, invoked once per file by the GLFW drop callback
		 *  (owner wires it). Arg: the FILE PATH in GLFW's UTF-8 encoding. */
		std::function<void(const char*)> OnDropFile;

	private:
		GLFWwindow* Window = nullptr;
	};
#endif // !MAHO_HEADLESS

#if defined(__linux__)
			// -- EGL headless backend (Linux, no OS window) --
	// Surfaceless EGL platform + pbuffer surface + OpenGL ES 2.0 context.

	class FEGLHeadlessWindow final : public IPlatform
	{
	public:
		FEGLHeadlessWindow(int Width, int Height)
		{
			Display = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
			if (Display == EGL_NO_DISPLAY || !eglInitialize(Display, nullptr, nullptr))
			{
				return;
			}
			eglBindAPI(EGL_OPENGL_ES_API);

			const EGLint ConfigAttribs[] = {
				EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
				EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
				EGL_NONE,
			};
			EGLConfig Config = nullptr;
			EGLint NumConfigs = 0;
			if (!eglChooseConfig(Display, ConfigAttribs, &Config, 1, &NumConfigs))
			{
				return;
			}

			const EGLint SurfaceAttribs[] = { EGL_WIDTH, Width, EGL_HEIGHT, Height, EGL_NONE };
			Surface = eglCreatePbufferSurface(Display, Config, SurfaceAttribs);
			if (Surface == EGL_NO_SURFACE)
			{
				return;
			}

			const EGLint ContextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
			Context = eglCreateContext(Display, Config, EGL_NO_CONTEXT, ContextAttribs);
			if (Context == EGL_NO_CONTEXT)
			{
				return;
			}

			eglMakeCurrent(Display, Surface, Surface, Context);
		}

		~FEGLHeadlessWindow() override
		{
			eglMakeCurrent(Display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
			if (Context != EGL_NO_CONTEXT)
			{
				eglDestroyContext(Display, Context);
			}
			if (Surface != EGL_NO_SURFACE)
			{
				eglDestroySurface(Display, Surface);
			}
			eglTerminate(Display);
		}

		[[nodiscard]] FNativeSurface GetNativeWindow() const override
		{
			return static_cast<FNativeSurface>(Context);
		}

	private:
		EGLDisplay Display = EGL_NO_DISPLAY;
		EGLSurface Surface = EGL_NO_SURFACE;
		EGLContext Context = EGL_NO_CONTEXT;
	};
#endif // __linux__

	struct FPlatformBackend
	{
		std::unique_ptr<IPlatform> Surface;
		std::function<void()> PollEvents;
		std::function<bool()> ShouldClose;
		std::function<GLFWwindow*()> GetGlfwWindow;
		std::function<void(std::function<void(int, int)>)> SetFramebufferSizeListener;
		std::function<void(std::function<void(double, double)>)> SetMouseWheelListener;
		std::function<void(std::function<void(double, double)>)> SetCursorPosListener;
		std::function<void(std::function<void(int, int, int)>)> SetMouseButtonListener;
		std::function<void(std::function<void(int, int, int, int)>)> SetKeyListener;
		std::function<void(std::function<void(std::uint32_t)>)> SetCharListener;
		std::function<void(std::function<void(bool)>)> SetCursorEnterListener;
		std::function<void(std::function<void(bool)>)> SetWindowFocusListener;
		std::function<void(std::function<void(const char*)>)> SetDropFileListener;
	};

	FPlatformBackend CreateWindowBackend(int Width, int Height, std::string_view Title)
	{
#if !defined(MAHO_HEADLESS) && (defined(_WIN32) || defined(__linux__))
		auto Backend = std::make_unique<FGlfwWindow>(Width, Height, Title);
		FGlfwWindow* Raw = Backend.get();
		return {
			std::move(Backend),
			[Raw]() { Raw->PollEvents(); },
			[Raw]() { return Raw->ShouldClose(); },
			[Raw]() { return Raw->GetGlfwWindow(); },
			[Raw](std::function<void(int, int)> Listener) { Raw->OnFramebufferSize = std::move(Listener); },
			[Raw](std::function<void(double, double)> Listener) { Raw->OnMouseWheel = std::move(Listener); },
			[Raw](std::function<void(double, double)> Listener) { Raw->OnCursorPos = std::move(Listener); },
			[Raw](std::function<void(int, int, int)> Listener) { Raw->OnMouseButton = std::move(Listener); },
			[Raw](std::function<void(int, int, int, int)> Listener) { Raw->OnKey = std::move(Listener); },
			[Raw](std::function<void(std::uint32_t)> Listener) { Raw->OnChar = std::move(Listener); },
			[Raw](std::function<void(bool)> Listener) { Raw->OnCursorEnter = std::move(Listener); },
			[Raw](std::function<void(bool)> Listener) { Raw->OnWindowFocus = std::move(Listener); },
			[Raw](std::function<void(const char*)> Listener) { Raw->OnDropFile = std::move(Listener); },
		};
#else
		return {};
#endif
	}

	FPlatformBackend CreateHeadlessBackend(int Width, int Height)
	{
#if defined(__linux__)
		auto Backend = std::make_unique<FEGLHeadlessWindow>(Width, Height);
		return { std::move(Backend), {}, {} };
#else
		return {};
#endif
	}
}

bool FPlatform::OnInitialize()
{
	// Runs ON the platform thread, before its loop starts -- i.e. before ANY window exists (GLFW's
	// hidden helper window included), which is the documented order for disabling the IME. It has to
	// be THIS thread: ImmDisableIME is thread-scoped, and this is the thread that will pump.
#if defined(_WIN32)
	::ImmDisableIME(-1);
#endif
	return true;
}

void FPlatform::OnShutdown()
{
	// Nothing to do: the window is destroyed by the IShutdown stage (DestroyWindow marshals here)
	// before FThreadedServer::Shutdown joins this thread.
}

const char* FPlatform::GetThreadName() const
{
	return "FPlatform";
}

void FPlatform::RunOnPlatformThread(const std::function<void()>& Fn)
{
	// Inline when already on the platform thread (a task must never wait on itself), and when the
	// thread is not running at all (construction / headless) -- else the wait below would never end.
	if (IsServerThread() || !IsRunning())
	{
		Fn();
		return;
	}

	std::mutex Done_Mutex;
	std::condition_variable Done_Cond;
	bool bDone = false;
	Submit([&]
	{
		Fn();
		{
			std::lock_guard<std::mutex> L(Done_Mutex);
			bDone = true;
		}
		Done_Cond.notify_all();
	});
	std::unique_lock<std::mutex> L(Done_Mutex);
	Done_Cond.wait(L, [&] { return bDone; });
}

void FPlatform::PublishWindowState()
{
	// Called ON the platform thread after a pump: the whole window state any other thread may read.
	// It is only ever written here, so a reader sees a coherent moment rather than a half-updated one.
	GLFWwindow* Win = GlfwWindowFn ? GlfwWindowFn() : nullptr;
	{
		std::lock_guard<std::mutex> L(NativeMutex);
		CachedToolkitWindow = Win;
		CachedSurface = Surface ? Surface->GetNativeWindow() : FNativeSurface{};
	}
	if (Win != nullptr)
	{
#if !defined(MAHO_HEADLESS)
		int W = 0;
		int H = 0;
		::glfwGetWindowSize(Win, &W, &H);
		CachedWidth.store(static_cast<std::uint32_t>(W > 0 ? W : 0), std::memory_order_relaxed);
		CachedHeight.store(static_cast<std::uint32_t>(H > 0 ? H : 0), std::memory_order_relaxed);
#endif
		bCachedShouldClose.store(QueryShouldClose ? QueryShouldClose() : false, std::memory_order_relaxed);
	}
	else
	{
		bCachedShouldClose.store(true, std::memory_order_relaxed);   // no window: nothing left to do
	}
}

void FPlatform::Initialize(FEngineBase&, FEngineContext&)
{
	MAHO_TRACE_STAGE(IInit, "Platform init", "start the platform thread and create the window");
	// The platform's OWN thread starts FIRST, because the window is created on it -- and only there.
	// A Win32 window's message queue belongs to its creating thread (PeekMessage sees nothing else),
	// so a window created on whichever pool worker happened to run this stage can never be pumped
	// properly: the pump runs on a different worker nearly every frame and asks the wrong queue.
	if (!FThreadedServer::Initialize())
	{
		MAHO_LOG_CORE_ERROR("FPlatform: the platform thread failed to start");
		return;
	}

	// Window size comes from the CVars; the Config layer already pushed the
	// [ConsoleVariables] ini values into them (r.Window.Width/Height).
	const int Width = GCVarWindowWidth.GetValue();
	const int Height = GCVarWindowHeight.GetValue();
	const std::string Title(GCVarWindowTitle.GetValue());

#if !defined(MAHO_HEADLESS)
	bool bOk = false;
	RunOnPlatformThread([this, Width, Height, &Title, &bOk] { bOk = CreateWindow(Width, Height, Title); });
	MAHO_LOG_CORE_INFO("FPlatform::Initialize - CreateWindow({}, {}) => {}", Width, Height, bOk);
#else
	CachedWidth.store(static_cast<std::uint32_t>(Width), std::memory_order_relaxed);
	CachedHeight.store(static_cast<std::uint32_t>(Height), std::memory_order_relaxed);
	(void)Title;
#endif

	GPlatform = this;
}

void FPlatform::Shutdown(FEngineBase&, FEngineContext&)
{
	MAHO_TRACE_STAGE(IShutdown, "Platform shutdown", "destroy the window and join the platform thread");
	GPlatform = nullptr;
	DestroyWindow();                 // marshals onto the platform thread
	FThreadedServer::Shutdown();     // ... and only then stop + join that thread
}

bool FPlatform::CreateWindow(int Width, int Height, std::string_view Title)
{
	// MUST run on the platform thread: the window is created here, and the thread that creates a
	// Win32 window owns its message queue. (The IME is disabled once in OnInitialize, on that same
	// thread and before any window -- including GLFW's hidden helper window -- exists.)
	DestroyWindow();

	FPlatformBackend Backend = CreateWindowBackend(Width, Height, Title);
	Surface = std::move(Backend.Surface);
	PollEventsFn = std::move(Backend.PollEvents);
	QueryShouldClose = std::move(Backend.ShouldClose);
	GlfwWindowFn = std::move(Backend.GetGlfwWindow);
	if (Backend.SetFramebufferSizeListener)
	{
		// GLFW fires this on the platform thread, during the pump. The RHI re-creates the swapchain
		// via VK_ERROR_OUT_OF_DATE on acquire; here the frame-side cache is what keeps ImGui's
		// DisplaySize / the window-layout size in sync with the OS window.
		Backend.SetFramebufferSizeListener([this](int NewWidth, int NewHeight)
		{
			CachedWidth.store(static_cast<std::uint32_t>(NewWidth), std::memory_order_relaxed);
			CachedHeight.store(static_cast<std::uint32_t>(NewHeight), std::memory_order_relaxed);
		});
	}
	if (Backend.SetMouseWheelListener)
	{
		// GLFW fires this on the PLATFORM thread, during the pump. The delta is accumulated behind
		// InputMutex and published with THIS frame's slot, so both ImGui contexts (the editor's and
		// the game's) read the same delta instead of one of them clearing it for the other.
		Backend.SetMouseWheelListener([this](double XOff, double YOff)
		{
			std::lock_guard<std::mutex> L(InputMutex);
			Input.MouseWheelX += static_cast<float>(XOff);
			Input.MouseWheelY += static_cast<float>(YOff);
			MInputEvent Ev;
			Ev.Type = MInputEventType::Scroll;
			Ev.X = static_cast<float>(XOff);
			Ev.Y = static_cast<float>(YOff);
			FrameEvents.push_back(Ev);
		});
	}
	if (Backend.SetCursorPosListener)
	{
		// Pull-current cursor snapshot + a MouseMove event: readers copy the snapshot (ReadInput,
		// repeatable) or walk the published frames by index (ReadInputFrame) -- reading consumes
		// nothing either way.
		Backend.SetCursorPosListener([this](double X, double Y)
		{
			std::lock_guard<std::mutex> L(InputMutex);
			Input.MouseX = static_cast<float>(X);
			Input.MouseY = static_cast<float>(Y);
			MInputEvent Ev;
			Ev.Type = MInputEventType::MouseMove;
			Ev.X = static_cast<float>(X);
			Ev.Y = static_cast<float>(Y);
			FrameEvents.push_back(Ev);
		});
	}
	if (Backend.SetMouseButtonListener)
	{
		Backend.SetMouseButtonListener([this](int Button, int Action, int Mods)
		{
			std::lock_guard<std::mutex> L(InputMutex);
			Input.Mods = static_cast<std::uint16_t>(Mods);
			if (Button >= 0 && Button < 3)
			{
				Input.MouseButtons[Button] = (Action != GLFW_RELEASE);
			}
			MInputEvent Ev;
			Ev.Type = MInputEventType::MouseButton;
			Ev.Key = Button;
			Ev.Action = static_cast<std::uint8_t>(Action);
			Ev.Mods = static_cast<std::uint16_t>(Mods);
			FrameEvents.push_back(Ev);
		});
	}
	if (Backend.SetKeyListener)
	{
		Backend.SetKeyListener([this](int Key, int Scancode, int Action, int Mods)
		{
			std::lock_guard<std::mutex> L(InputMutex);
			Input.Mods = static_cast<std::uint16_t>(Mods);
			if (Key >= 0 && Key < MInputContext::KeyCount)
			{
				Input.KeyDown[Key] = (Action != GLFW_RELEASE);
			}
			MInputEvent Ev;
			Ev.Type = MInputEventType::Key;
			Ev.Key = Key;
			Ev.Scancode = Scancode;
			Ev.Action = static_cast<std::uint8_t>(Action);
			Ev.Mods = static_cast<std::uint16_t>(Mods);
			FrameEvents.push_back(Ev);
		});
	}
	if (Backend.SetCharListener)
	{
		Backend.SetCharListener([this](std::uint32_t Codepoint)
		{
			std::lock_guard<std::mutex> L(InputMutex);
			MInputEvent Ev;
			Ev.Type = MInputEventType::Char;
			Ev.Codepoint = Codepoint;
			FrameEvents.push_back(Ev);
		});
	}
	if (Backend.SetCursorEnterListener)
	{
		Backend.SetCursorEnterListener([this](bool Entered)
		{
			std::lock_guard<std::mutex> L(InputMutex);
			Input.MouseEntered = Entered;
			MInputEvent Ev;
			Ev.Type = MInputEventType::CursorEnter;
			Ev.Bool = Entered;
			FrameEvents.push_back(Ev);
		});
	}
	if (Backend.SetWindowFocusListener)
	{
		Backend.SetWindowFocusListener([this](bool Focused)
		{
			std::lock_guard<std::mutex> L(InputMutex);
			Input.WindowFocused = Focused;

			// Keyboard follows FOCUS, not the pointer -- so it does not suffer the bug the mouse
			// had (a drag leaving the window keeps its key events, because the window keeps focus).
			// Its failure mode is the transition instead: once focus is gone the OS delivers the
			// KEYUP to whoever took it, so a key held at that moment stays "down" forever in the
			// snapshot. Clear the down-state on the transition -- "nothing is held while we are
			// not focused" is the truth, and it makes the stuck-key state unreachable.
			if (!Focused)
			{
				for (bool& Down : Input.KeyDown)
				{
					Down = false;
				}
				Input.Mods = 0;
			}

			MInputEvent Ev;
			Ev.Type = MInputEventType::WindowFocus;
			Ev.Bool = Focused;
			FrameEvents.push_back(Ev);
		});
	}
	if (Backend.SetDropFileListener)
	{
		// OS file drop. GLFW reports the paths as UTF-8; they are converted to the
		// engine's narrow-char convention here (one boundary) and queued as PHYSICAL
		// ABSOLUTE paths. No virtual-path mapping happens at this layer.
		Backend.SetDropFileListener([this](const char* Utf8Path)
		{
			std::string Path = ToNativeNarrowPath(Utf8Path);
			if (Path.empty())
			{
				return;
			}
			std::lock_guard<std::mutex> L(InputMutex);
			DroppedFiles.push_back(std::move(Path));
		});
	}

	// Publish the window state IMMEDIATELY: other layers (the RHI takes the native handle to create
	// its surface/swapchain) read the cache as soon as their own init runs, which is ordered after
	// this stage but long before the first pump. Waiting for the pump left them without a window.
	PublishWindowState();
	return Surface != nullptr && Surface->GetNativeWindow() != nullptr;
}

bool FPlatform::CreateHeadlessContext(int Width, int Height)
{
	DestroyWindow();
	FPlatformBackend Backend = CreateHeadlessBackend(Width, Height);
	Surface = std::move(Backend.Surface);
	PollEventsFn = std::move(Backend.PollEvents);
	QueryShouldClose = std::move(Backend.ShouldClose);
	GlfwWindowFn = std::move(Backend.GetGlfwWindow);
	PublishWindowState();
	return Surface != nullptr && Surface->GetNativeWindow() != nullptr;
}

void FPlatform::DestroyWindow()
{
	// Marshall to the platform thread: this releases the surface and drops the GLFW handles, all of
	// which belong to the thread that created the window (and the callbacks it removed were firing
	// there).
	RunOnPlatformThread([this]
	{
		// Teardown the surface; the GLFW callbacks it owned are gone now, so drop any
		// in-flight accumulated input (a stale event must not leak into a new window).
		std::lock_guard<std::mutex> L(InputMutex);
		Surface.reset();
		PollEventsFn = {};
		QueryShouldClose = {};
		GlfwWindowFn = {};
		Input = {};
		FrameEvents.clear();
		{
			std::lock_guard<std::mutex> N(NativeMutex);
			CachedToolkitWindow = nullptr;
			CachedSurface = FNativeSurface{};
		}
		CachedWidth.store(0, std::memory_order_relaxed);
		CachedHeight.store(0, std::memory_order_relaxed);
		bCachedShouldClose.store(true, std::memory_order_relaxed);
		// Drop the published ring too: a stale frame must not be readable by a new window's consumers
		// (they align on the index, so resetting it makes them start clean).
		for (FInputFrame& Slot : InputRing)
		{
			Slot = FInputFrame{};
		}
		InputFrameCounter = 0;
		InputFramePublished.store(0, std::memory_order_release);
		DroppedFiles.clear();
	});
}

// PollEvents is where the WINDOW's own (non-client) traffic is serviced -- the pump is called once
// per frame, and DefWindowProc answers a caption/border hit from inside it. Two consequences worth
// remembering, both measured on this machine and neither of them a bug in this file:
//
//  * A caption drag or a border resize enters a MODAL move loop inside DefWindowProc, i.e. inside
//    glfwPollEvents, i.e. inside our frame: the pump does not return until the drag ends. Measured
//    gates of 0.99s / 0.52s / 0.34s / 14.26s, with the render loop otherwise never exceeding ~130ms,
//    and keystrokes typed meanwhile arriving in one burst up to 1.16s late. Any pump-in-a-while-loop
//    app has this; an event/timer-driven one (Tk, Qt) escapes it, which is why the control test --
//    the SAME Tk rewritten as `while: root.update()` -- froze for 606/2036/5743ms. Taking the drag
//    over (record the grab, SetWindowPos per frame, end on button-up, ignore WM_CAPTURECHANGED) is
//    the fix if it is ever worth losing the system's snap / maximize-on-double-click.
//  * IME: without ImmDisableIME every keystroke is offered to the input method first (the WM_KEYDOWNs
//    carry VK_PROCESSKEY) and characters come back only when it commits -- measured as keys queued
//    0.6-4s and six WM_CHARs landing in one batch. The thread-wide ImmDisableIME call in CreateWindow
//    (before any window exists) is the documented order; a per-window ImmAssociateContext(hwnd,
//    nullptr) hung the startup here, so it stays out.
void FPlatform::PollEvents()
{
	if (PollEventsFn)
	{
		PollEventsFn();
	}

#if defined(_WIN32)
	// Pull the pointer position AND the button state straight from the OS, bypassing the window.
	//
	// GLFW's callbacks only fire for events DELIVERED TO THIS WINDOW, so a drag that leaves the
	// client area loses whatever the window no longer receives -- most visibly the RELEASE. With
	// the button stuck "down" in the snapshot, ImGui keeps its MovingWindow alive and the window
	// keeps following the cursor after the user let go. The asymmetry is the signature: the
	// position kept tracking (it already had a global source) while the button did not.
	//
	// GetCursorPos / GetAsyncKeyState are GLOBAL queries -- they report the real state no matter
	// which window is under the pointer or has focus. Inside the window they agree with the
	// callbacks, so this only fills the gap the callbacks cannot cover.
	if (GLFWwindow* ToolkitWindow = GetToolkitWindowHandle())
	{
		if (HWND Hwnd = glfwGetWin32Window(ToolkitWindow))
		{
			std::lock_guard<std::mutex> L(InputMutex);

			POINT P{};
			if (::GetCursorPos(&P) != 0 && ::ScreenToClient(Hwnd, &P) != 0)
			{
				Input.MouseX = static_cast<float>(P.x);
				Input.MouseY = static_cast<float>(P.y);
			}

			Input.MouseButtons[0] = (::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
			Input.MouseButtons[1] = (::GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
			Input.MouseButtons[2] = (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
		}
	}
#endif

	// Last: the window state the REST of the engine reads must be published from this thread, after
	// the messages above have been dispatched (a size change or a close request is one of them).
	PublishWindowState();
}

// -- engine loop stages (FEngineLayer) --

void FPlatform::Tick(FEngineBase& Engine, FEngineContext& Frame)
{
	MAHO_TRACE_STAGE(ITick, "Platform tick", "pump the window messages and publish this frame's input");
	// The pump runs on the PLATFORM thread, marshalled (and waited on) from whatever thread this
	// stage was dispatched to. That is the whole point: the window's messages live in the queue of
	// the thread that created it, and PeekMessage looks nowhere else -- pumping from a random pool
	// worker left the keys sitting in the queue for seconds and then delivered them in a burst.
	RunOnPlatformThread([this] { PollEvents(); });

	// Publish THIS frame's input as one immutable, TAGGED slot: the snapshot plus the edge events
	// that arrived during this same pump.
	//
	//  1. Publishing (rather than letting readers pull the live copy) is what makes the input
	//     consumable from ANOTHER frame graph -- the UI stages are children of the render collector,
	//     so no dependency edge can order them against this stage and a pull would race the pump.
	//  2. Carrying the frame's INDEX is what lets several consumers share the input without stealing
	//     it from each other: each keeps its own cursor and applies every frame exactly once, in
	//     order. The old design handed the batch to whoever drained first, so a consumer that missed
	//     the frame carrying a key RELEASE never learned the key came up -- ImGui then held it down
	//     and auto-repeated it (one Backspace erasing a whole line, one arrow walking to the far left).
	//
	// The wheel is accumulated by the callbacks and MOVED into the slot here, so it becomes per-frame
	// data every consumer can read instead of a single-consumer exchange-to-zero.
	std::uint64_t Published = 0;
	{
		std::lock_guard<std::mutex> L(InputMutex);
		++InputFrameCounter;
		InputRingWrite = static_cast<std::uint32_t>((InputFrameCounter - 1) % kInputRingSlots);
		FInputFrame& Slot = InputRing[InputRingWrite];
		Slot.Index = InputFrameCounter;
		Slot.Snapshot = Input;
		Slot.Events = std::move(FrameEvents);
		FrameEvents.clear();
		Input.MouseWheelX = 0.0f;
		Input.MouseWheelY = 0.0f;
		Published = InputFrameCounter;
		InputFramePublished.store(InputFrameCounter, std::memory_order_release);
	}

	// OUTSIDE the lock: a subscriber may reach straight back in (ReadInputFrame), which is exactly
	// why the delegate invokes its handlers outside its own lock.
	OnInputPublished.Broadcast(Published);
}

void FPlatform::ReadFrameInput(MInputContext& Out) const
{
	std::lock_guard<std::mutex> L(InputMutex);
	const std::uint64_t Latest = InputFramePublished.load(std::memory_order_acquire);
	if (Latest == 0)
	{
		Out = Input;   // nothing published yet: hand back the live copy
		return;
	}
	Out = InputRing[(Latest - 1) % kInputRingSlots].Snapshot;
}

std::uint64_t FPlatform::GetInputFrameIndex() const
{
	return InputFramePublished.load(std::memory_order_acquire);
}

bool FPlatform::ReadInputFrame(std::uint64_t Index, FInputFrame& Out) const
{
	std::lock_guard<std::mutex> L(InputMutex);
	if (Index == 0)
	{
		return false;
	}
	const FInputFrame& Slot = InputRing[(Index - 1) % kInputRingSlots];
	if (Slot.Index != Index)
	{
		return false;   // evicted, or never written -- the caller fell too far behind
	}
	Out = Slot;
	return true;
}

void FPlatform::EndFrame(FEngineBase&, FEngineContext&)
{
	MAHO_TRACE_STAGE(IEndFrame, "Platform frame end", "the input snapshot was frozen at tick");
	// Nothing to do: Tick() publishes the frame's input into the ring and drains the event
	// stream, so the state a reader observes is already frozen at that point.
}

void FPlatform::RequestExit(FEngineBase& Engine, FEngineContext& Frame)
{
	MAHO_TRACE_STAGE(IExit, "Platform exit", "turn the close request into an engine exit");
	// window close request -> tell the host engine to exit the main loop.
	if (ShouldClose())
	{
		Engine.RequestExit();
	}
}

FNativeSurface FPlatform::GetNativeWindow() const
{
	// The cached handle, not a fresh call into the backend: producing it means calling into GLFW,
	// which only the platform thread may do. The cache is refreshed at the end of every pump.
	std::lock_guard<std::mutex> L(NativeMutex);
	return CachedSurface;
}

bool FPlatform::ShouldClose() const
{
	// The close request is one of the messages the pump dispatches, so the answer is cached by
	// PublishWindowState on the platform thread -- never queried through GLFW from here.
	return bCachedShouldClose.load(std::memory_order_relaxed);
}

void FPlatform::ReadInput(MInputContext& Out) const
{
	std::lock_guard<std::mutex> L(InputMutex);
	// Copy the whole LIVE working copy (mouse pos/buttons + keyboard down-state + mods + cursor-in
	// + focus). Its wheel fields are whatever the current pump accumulated -- a reader that wants a
	// FRAME's input, wheel included, wants ReadFrameInput / ReadInputFrame instead.
	Out = Input;
}

void FPlatform::DrainDroppedFiles(std::vector<std::string>& Out) const
{
	std::lock_guard<std::mutex> L(InputMutex);
	Out.insert(Out.end(), DroppedFiles.begin(), DroppedFiles.end());
	DroppedFiles.clear();
}

} // namespace Maho::Platform

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_API Maho::FFrameExtension* CreateFrame()
{
	return Maho::Platform::FPlatform::CreateFrame();
}
