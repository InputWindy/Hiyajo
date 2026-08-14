#pragma once

#include <Core/Extension/Platform/PlatformWindow.h>

#include <mutex>
#include <vector>

struct GLFWwindow;

namespace Maho
{

/** GLFW-backed FPlatformWindow. Not part of the public Maho API surface. */
class FGlfwPlatformWindow final : public FPlatformWindow
{
public:
	FGlfwPlatformWindow() = default;
	~FGlfwPlatformWindow() override;

	[[nodiscard]] bool Initialize(const FPlatformWindowDesc& Desc);

	virtual void Destroy() override;

	[[nodiscard]] virtual bool IsValid() const override;
	[[nodiscard]] virtual bool HasOsWindow() const override;
	[[nodiscard]] virtual bool ShouldClose() const override;

	virtual void SetTitle(const std::string& Title) override;
	virtual void GetFramebufferSize(int& OutWidth, int& OutHeight) const override;
	[[nodiscard]] virtual void* GetNativeHandle() const override;
	[[nodiscard]] virtual void* GetToolkitWindowHandle() const override;

	virtual void PollEvents() override;
	[[nodiscard]] virtual double GetTimeSeconds() const override;
	virtual void DrainDroppedFilePaths(std::vector<std::string>& OutPaths) override;

	/** Process-wide glfwInit / glfwTerminate helpers used by the factory. */
	[[nodiscard]] static bool EnsureRuntimeInitialized();
	static void ShutdownRuntime();
	[[nodiscard]] static bool IsRuntimeInitialized();

private:
	static void OnGlfwDrop(GLFWwindow* Window, int PathCount, const char** Paths);

	GLFWwindow* Handle = nullptr;
	bool bRuntimeOwned = false;
	std::mutex DropMutex;
	std::vector<std::string> PendingDroppedPaths;
};

} // namespace Maho
