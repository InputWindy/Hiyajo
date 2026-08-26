#pragma once

#include "RHIAPI.h"
#include <Core/Singleton.h>
#include <Core/ThreadedServer.h>
#include <Engine/Layer.h>
#include "RHI.h"

#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace Maho
{

/**
 * RHI service - backend-agnostic GPU device surface (engine Common, TSingleton).
 * Owns the MahoRHI worker thread (FThreadedServer) which hosts the IRHI device
 * and executes frame submissions. Backend-agnostic - higher layers (RDG /
 * render plugin) submit work here, never touching a concrete backend type.
 */
class FRHISystem
	: public TSingleton<FRHISystem>
	, public FThreadedServer
	, public IPlugin<IInit, IShutdown>
{
public:
	/** Process-unique accessor - defined in RHI.cpp (in RHI.dll). */
	static FRHISystem& Get();

	~FRHISystem() override;

	void Initialize(int Argc, char** Argv) override;
	void Shutdown() override;

	[[nodiscard]] bool HasRHI() const
	{
		return static_cast<bool>(RHI);
	}
	/** The backend-agnostic device (IRHI) - never downcast to a concrete backend. */
	[[nodiscard]] IRHI* GetRHI() const
	{
		return RHI.get();
	}

	void WaitForRenderFrame(std::uint64_t FrameIndex);
	void SignalRenderFrameComplete(std::uint64_t FrameIndex);
	void ResetFrameFence();

	[[nodiscard]] bool InitializeRHI(void* NativeWindowHandle, int Width, int Height,
		ERHIBackend Backend = ERHIBackend::Vulkan);
	void ShutdownRHI();

	void SubmitBeginFrame(float R, float G, float B, float A);
	void SubmitEndFrame(std::uint64_t FrameIndex);
	void RequestResize(int Width, int Height);

protected:
	friend TSingleton<FRHISystem>;
	FRHISystem();

	[[nodiscard]] const char* GetThreadName() const override
	{
		return "MahoRHI";
	}

	bool OnInitialize() override;
	void OnShutdown() override;

private:
	FRHIPtr RHI;

	std::mutex FenceMutex;
	std::condition_variable FenceCv;
	std::uint64_t LastCompletedRenderFrame = 0;
};

} // namespace Maho
