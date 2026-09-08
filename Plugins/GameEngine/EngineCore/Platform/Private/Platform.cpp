#include "Platform.h"

#include <Config.h>
#include <ConsoleVariable.h>
#include <Log.h>

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

namespace Maho::Platform
{

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
		"r.Window.Width", 1280, "Platform window width");

	static ConsoleVariable::TAutoConsoleVariable<int> GCVarWindowHeight(
		"r.Window.Height", 720, "Platform window height");

	static ConsoleVariable::TAutoConsoleVariable<std::string> GCVarWindowTitle(
		"r.Window.Title", "Maho", "Platform window title");

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
					// Mouse wheel is a discrete event GLFW consumes during PollEvents, so a
					// no-backend ImGui context can't poll it via key state. Accumulate the
					// scroll delta here and let the owner consume it (ConsumeMouseWheelY).
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

void FPlatform::Initialize(FEngineBase&)
{
	// Window size comes from the CVars; the Config layer already pushed the
	// [ConsoleVariables] ini values into them (r.Window.Width/Height).
	WindowWidth = static_cast<std::uint32_t>(GCVarWindowWidth.GetValue());
	WindowHeight = static_cast<std::uint32_t>(GCVarWindowHeight.GetValue());

#if !defined(MAHO_HEADLESS)
	const bool bOk = CreateWindow(WindowWidth, WindowHeight, GCVarWindowTitle.GetValue());
	MAHO_LOG_CORE_INFO("FPlatform::Initialize - CreateWindow({}, {}) => {}", WindowWidth, WindowHeight, bOk);
#endif

	GPlatform = this;
}

void FPlatform::Shutdown(FEngineBase&)
{
	GPlatform = nullptr;
	DestroyWindow();
}

bool FPlatform::CreateWindow(int Width, int Height, std::string_view Title)
{
	DestroyWindow();
	FPlatformBackend Backend = CreateWindowBackend(Width, Height, Title);
	Surface = std::move(Backend.Surface);
	PollEventsFn = std::move(Backend.PollEvents);
	QueryShouldClose = std::move(Backend.ShouldClose);
	GlfwWindowFn = std::move(Backend.GetGlfwWindow);
	if (Backend.SetFramebufferSizeListener)
	{
		// GLFW fires this on-frame (during PollEvents, the engine's Tick thread) so
		// the live framebuffer size stays current. The RHI re-creates the swapchain
		// via VK_ERROR_OUT_OF_DATE on acquire; here we keep the ImGui DisplaySize /
		// window-layout size in sync with the OS window.
		Backend.SetFramebufferSizeListener([this](int Width, int Height)
		{
			WindowWidth = static_cast<std::uint32_t>(Width);
			WindowHeight = static_cast<std::uint32_t>(Height);
		});
	}
	if (Backend.SetMouseWheelListener)
	{
		// GLFW fires this on-frame (during PollEvents, the engine's Tick thread). The
		// editor's ImGui context runs on the render thread; the delta is accumulated
		// behind InputMutex and a reader exchanges it to zero (ConsumeMouseWheelXY).
		Backend.SetMouseWheelListener([this](double XOff, double YOff)
		{
			std::lock_guard<std::mutex> L(InputMutex);
			Input.MouseWheelX += static_cast<float>(XOff);
			Input.MouseWheelY += static_cast<float>(YOff);
			MInputEvent Ev;
			Ev.Type = MInputEventType::Scroll;
			Ev.X = static_cast<float>(XOff);
			Ev.Y = static_cast<float>(YOff);
			InputEvents.push_back(Ev);
		});
	}
	if (Backend.SetCursorPosListener)
	{
		// Pull-current cursor snapshot + a MouseMove event; the reader copies it
		// (ReadInput) without consuming, or drains the stream (DrainInputEvents).
		Backend.SetCursorPosListener([this](double X, double Y)
		{
			std::lock_guard<std::mutex> L(InputMutex);
			Input.MouseX = static_cast<float>(X);
			Input.MouseY = static_cast<float>(Y);
			MInputEvent Ev;
			Ev.Type = MInputEventType::MouseMove;
			Ev.X = static_cast<float>(X);
			Ev.Y = static_cast<float>(Y);
			InputEvents.push_back(Ev);
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
			InputEvents.push_back(Ev);
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
			InputEvents.push_back(Ev);
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
			InputEvents.push_back(Ev);
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
			InputEvents.push_back(Ev);
		});
	}
	if (Backend.SetWindowFocusListener)
	{
		Backend.SetWindowFocusListener([this](bool Focused)
		{
			std::lock_guard<std::mutex> L(InputMutex);
			Input.WindowFocused = Focused;
			MInputEvent Ev;
			Ev.Type = MInputEventType::WindowFocus;
			Ev.Bool = Focused;
			InputEvents.push_back(Ev);
		});
	}
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
	return Surface != nullptr && Surface->GetNativeWindow() != nullptr;
}

void FPlatform::DestroyWindow()
{
	// Teardown the surface; the GLFW callbacks it owned are gone now, so drop any
	// in-flight accumulated input (a stale event must not leak into a new window).
	std::lock_guard<std::mutex> L(InputMutex);
	Surface.reset();
	PollEventsFn = {};
	QueryShouldClose = {};
	GlfwWindowFn = {};
	Input = {};
	InputEvents.clear();
}

void FPlatform::PollEvents()
{
	if (PollEventsFn)
	{
		PollEventsFn();
	}
}

// -- engine loop stages (FEngineLayer) --

void FPlatform::BeginFrame(FEngineBase&)
{
}

void FPlatform::Tick(FEngineBase& Engine)
{
	PollEvents();
}

void FPlatform::EndFrame(FEngineBase&)
{
}

void FPlatform::RequestExit(FEngineBase& Engine)
{
	// window close request -> tell the host engine to exit the main loop.
	if (ShouldClose())
	{
		Engine.RequestExit();
	}
}

FNativeSurface FPlatform::GetNativeWindow() const
{
	return Surface != nullptr ? Surface->GetNativeWindow() : nullptr;
}

bool FPlatform::ShouldClose() const
{
	return QueryShouldClose && QueryShouldClose();
}

void FPlatform::ReadInput(MInputContext& Out) const
{
	std::lock_guard<std::mutex> L(InputMutex);
	// Copy the whole snapshot (mouse pos/buttons + keyboard down-state + mods + cursor-in
	// + focus). The wheel fields are stale accumulated values here -- consumers use
	// ConsumeMouseWheelXY() for the delta.
	Out = Input;
}

void FPlatform::DrainInputEvents(std::vector<MInputEvent>& Out) const
{
	std::lock_guard<std::mutex> L(InputMutex);
	Out.insert(Out.end(), InputEvents.begin(), InputEvents.end());
	InputEvents.clear();
}

void FPlatform::ConsumeMouseWheelXY(float& OutX, float& OutY)
{
	std::lock_guard<std::mutex> L(InputMutex);
	OutX = Input.MouseWheelX;
	OutY = Input.MouseWheelY;
	Input.MouseWheelX = 0.0f;
	Input.MouseWheelY = 0.0f;
}

float FPlatform::ConsumeMouseWheelY()
{
	std::lock_guard<std::mutex> L(InputMutex);
	const float V = Input.MouseWheelY;
	Input.MouseWheelY = 0.0f;
	return V;
}

} // namespace Maho::Platform

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_API Maho::FLayerBase* CreateLayer()
{
	return Maho::Platform::FPlatform::CreateLayer();
}
