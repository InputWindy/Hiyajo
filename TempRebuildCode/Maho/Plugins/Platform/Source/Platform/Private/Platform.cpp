#include <Platform.h>

#if !defined(MAHO_HEADLESS)
#	include <GLFW/glfw3.h>
#endif

#if defined(__linux__)
#	define EGL_EGLEXT_PROTOTYPES 1
#	include <EGL/egl.h>
#	include <EGL/eglext.h>
#endif

namespace Maho::Platform
{

namespace
{
#if !defined(MAHO_HEADLESS)
	// ── GLFW backend (desktop: Windows / Linux, windowed) ──

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
			return static_cast<FNativeSurface>(Window);
		}

		void PollEvents() { glfwPollEvents(); }
		[[nodiscard]] bool ShouldClose() const { return Window != nullptr && glfwWindowShouldClose(Window); }

	private:
		GLFWwindow* Window = nullptr;
	};
#endif // !MAHO_HEADLESS

#if defined(__linux__)
	// ── EGL headless backend (Linux, no OS window) ──
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

bool FPlatformSystem::ExecuteStage(EEngineStage Stage)
{
	switch (Stage)
	{
	case EEngineStage::PreTick:
	case EEngineStage::Tick:
		if (PollEvents)
		{
			PollEvents();
		}
		break;

	case EEngineStage::Shutdown:
		DestroyWindow();
		break;

	default:
		break;
	}
	return true;
}

bool FPlatformSystem::CreateWindow(int Width, int Height, std::string_view Title)
{
	DestroyWindow();
	FPlatformBackend Backend = CreateWindowBackend(Width, Height, Title);
	Surface = std::move(Backend.Surface);
	PollEvents = std::move(Backend.PollEvents);
	QueryShouldClose = std::move(Backend.ShouldClose);
	return Surface != nullptr && Surface->GetNativeWindow() != nullptr;
}

bool FPlatformSystem::CreateHeadlessContext(int Width, int Height)
{
	DestroyWindow();
	FPlatformBackend Backend = CreateHeadlessBackend(Width, Height);
	Surface = std::move(Backend.Surface);
	PollEvents = std::move(Backend.PollEvents);
	QueryShouldClose = std::move(Backend.ShouldClose);
	return Surface != nullptr && Surface->GetNativeWindow() != nullptr;
}

void FPlatformSystem::DestroyWindow()
{
	Surface.reset();
	PollEvents = {};
	QueryShouldClose = {};
}

FNativeSurface FPlatformSystem::GetNativeWindow() const
{
	return Surface != nullptr ? Surface->GetNativeWindow() : nullptr;
}

bool FPlatformSystem::ShouldClose() const
{
	return QueryShouldClose && QueryShouldClose();
}

} // namespace Maho::Platform

// ── Dynamic plugin entry (runtime load/unload via FPluginManager) ──

namespace
{

class FPlatformSystemAdapter final : public Maho::IExtension<Maho::EEngineStage>
{
public:
	[[nodiscard]] bool ExecuteStage(Maho::EEngineStage Stage) override
	{
		return Maho::Platform::FPlatformSystem::Get().ExecuteStage(Stage);
	}
};

} // namespace

extern "C" MAHO_PLATFORM_API Maho::IExtension<Maho::EEngineStage>* CreateExtension()
{
	return new FPlatformSystemAdapter();
}
