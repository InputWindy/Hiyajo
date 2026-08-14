#include "Core/Extension/Platform/GlfwPlatformWindow.h"

#include <Core/Misc/Log.h>

#include <GLFW/glfw3.h>

#if defined(_WIN32)
#	define GLFW_EXPOSE_NATIVE_WIN32
#	include <GLFW/glfw3native.h>
#endif

namespace Maho
{

namespace
{

bool GbGlfwRuntimeInitialized = false;

void GlfwErrorCallback(int ErrorCode, const char* Description)
{
	MAHO_CORE_ERROR("GLFW error {}: {}", ErrorCode, Description ? Description : "(null)");
}

} // namespace

bool FGlfwPlatformWindow::EnsureRuntimeInitialized()
{
	if (GbGlfwRuntimeInitialized)
	{
		return true;
	}

	glfwSetErrorCallback(GlfwErrorCallback);

	if (glfwInit() != GLFW_TRUE)
	{
		MAHO_CORE_ERROR("FGlfwPlatformWindow: glfwInit failed");
		return false;
	}

	GbGlfwRuntimeInitialized = true;
	MAHO_CORE_INFO("Platform runtime initialized (GLFW {})", glfwGetVersionString());
	return true;
}

void FGlfwPlatformWindow::ShutdownRuntime()
{
	if (!GbGlfwRuntimeInitialized)
	{
		return;
	}

	glfwTerminate();
	GbGlfwRuntimeInitialized = false;
	MAHO_CORE_INFO("Platform runtime shut down");
}

bool FGlfwPlatformWindow::IsRuntimeInitialized()
{
	return GbGlfwRuntimeInitialized;
}

FGlfwPlatformWindow::~FGlfwPlatformWindow()
{
	Destroy();
}

bool FGlfwPlatformWindow::Initialize(const FPlatformWindowDesc& Desc)
{
	if (bRuntimeOwned || Handle)
	{
		MAHO_CORE_ERROR("FGlfwPlatformWindow::Initialize: already initialized");
		return false;
	}

	if (!EnsureRuntimeInitialized())
	{
		return false;
	}

	bRuntimeOwned = true;

	if (Desc.bHeadless)
	{
		MAHO_CORE_INFO("FGlfwPlatformWindow created (headless)");
		return true;
	}

	if (Desc.Width <= 0 || Desc.Height <= 0)
	{
		MAHO_CORE_ERROR("FGlfwPlatformWindow::Initialize: invalid size {}x{}", Desc.Width, Desc.Height);
		return false;
	}

	glfwDefaultWindowHints();
	// No OpenGL/Vulkan context here — surface comes later via Vulkan WSI.
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, Desc.bResizable ? GLFW_TRUE : GLFW_FALSE);

	Handle = glfwCreateWindow(
		Desc.Width,
		Desc.Height,
		Desc.Title.c_str(),
		nullptr,
		nullptr);

	if (!Handle)
	{
		MAHO_CORE_ERROR("FGlfwPlatformWindow::Initialize: glfwCreateWindow failed");
		return false;
	}

	glfwSetWindowUserPointer(Handle, this);
	glfwSetDropCallback(Handle, &FGlfwPlatformWindow::OnGlfwDrop);

	MAHO_CORE_INFO("FGlfwPlatformWindow created: \"{}\" {}x{}", Desc.Title, Desc.Width, Desc.Height);
	return true;
}

void FGlfwPlatformWindow::OnGlfwDrop(GLFWwindow* Window, int PathCount, const char** Paths)
{
	if (!Window || PathCount <= 0 || !Paths)
	{
		return;
	}

	auto* Self = static_cast<FGlfwPlatformWindow*>(glfwGetWindowUserPointer(Window));
	if (!Self)
	{
		return;
	}

	std::lock_guard<std::mutex> Lock(Self->DropMutex);
	Self->PendingDroppedPaths.reserve(Self->PendingDroppedPaths.size() + static_cast<std::size_t>(PathCount));
	for (int Index = 0; Index < PathCount; ++Index)
	{
		if (Paths[Index] && Paths[Index][0] != '\0')
		{
			Self->PendingDroppedPaths.emplace_back(Paths[Index]);
		}
	}
}

void FGlfwPlatformWindow::DrainDroppedFilePaths(std::vector<std::string>& OutPaths)
{
	OutPaths.clear();
	std::lock_guard<std::mutex> Lock(DropMutex);
	OutPaths.swap(PendingDroppedPaths);
}

void FGlfwPlatformWindow::Destroy()
{
	if (Handle)
	{
		glfwSetDropCallback(Handle, nullptr);
		glfwSetWindowUserPointer(Handle, nullptr);
		glfwDestroyWindow(Handle);
		Handle = nullptr;
		MAHO_CORE_INFO("FGlfwPlatformWindow OS window destroyed");
	}

	{
		std::lock_guard<std::mutex> Lock(DropMutex);
		PendingDroppedPaths.clear();
	}

	bRuntimeOwned = false;
}

bool FGlfwPlatformWindow::IsValid() const
{
	return bRuntimeOwned || Handle != nullptr;
}

bool FGlfwPlatformWindow::HasOsWindow() const
{
	return Handle != nullptr;
}

bool FGlfwPlatformWindow::ShouldClose() const
{
	return Handle != nullptr && glfwWindowShouldClose(Handle) == GLFW_TRUE;
}

void FGlfwPlatformWindow::SetTitle(const std::string& Title)
{
	if (Handle)
	{
		glfwSetWindowTitle(Handle, Title.c_str());
	}
}

void FGlfwPlatformWindow::GetFramebufferSize(int& OutWidth, int& OutHeight) const
{
	OutWidth = 0;
	OutHeight = 0;
	if (Handle)
	{
		glfwGetFramebufferSize(Handle, &OutWidth, &OutHeight);
	}
}

void* FGlfwPlatformWindow::GetNativeHandle() const
{
#if defined(_WIN32)
	return Handle ? static_cast<void*>(glfwGetWin32Window(Handle)) : nullptr;
#else
	return nullptr;
#endif
}

void* FGlfwPlatformWindow::GetToolkitWindowHandle() const
{
	return Handle;
}

void FGlfwPlatformWindow::PollEvents()
{
	if (GbGlfwRuntimeInitialized)
	{
		glfwPollEvents();
	}
}

double FGlfwPlatformWindow::GetTimeSeconds() const
{
	return GbGlfwRuntimeInitialized ? glfwGetTime() : 0.0;
}

void FPlatformWindowDeleter::operator()(FPlatformWindow* Window) const
{
	delete Window;
}

FPlatformWindowPtr FPlatformWindowFactory::Create(const FPlatformWindowDesc& Desc)
{
	switch (Desc.Platform)
	{
	case EPlatform::Glfw:
	{
		auto* Window = new FGlfwPlatformWindow();
		if (!Window->Initialize(Desc))
		{
			delete Window;
			return nullptr;
		}
		return FPlatformWindowPtr{Window};
	}
	}

	MAHO_CORE_ERROR("FPlatformWindowFactory::Create: unsupported EPlatform ({})", static_cast<std::uint32_t>(Desc.Platform));
	return nullptr;
}

void FPlatformWindowFactory::Shutdown()
{
	FGlfwPlatformWindow::ShutdownRuntime();
}

} // namespace Maho
