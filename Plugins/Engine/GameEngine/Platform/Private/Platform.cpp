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
	Surface.reset();
	PollEventsFn = {};
	QueryShouldClose = {};
	GlfwWindowFn = {};
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

} // namespace Maho::Platform

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_API Maho::FLayerBase* CreateLayer()
{
	return Maho::Platform::FPlatform::CreateLayer();
}
