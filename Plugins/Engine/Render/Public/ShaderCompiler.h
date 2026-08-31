#pragma once

#include "RenderApi.h"
#include <Core/ThreadedServer.h>
#include <RHI/RHIEnums.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Maho
{

/** One compiled shader stage (SPIR-V bytecode). */
struct FShaderCompileResult
{
	bool bSuccess = false;
	std::vector<std::uint32_t> Bytecode;   // SPIR-V words
	std::string ErrorLog;
};

/** Shader compile request (GLSL source + stage + entry point). */
struct FShaderCompileDesc
{
	std::string Source;                    // GLSL source
	ERHIShaderStage Stage = ERHIShaderStage::Vertex;
	std::string EntryPoint = "main";
};

/**
 * Async shader compiler - a dedicated compile thread (FThreadedServer). The
 * render owner (FRender) holds it; CompileAsync submits a request and the
 * callback fires on the CALLER thread after the compile completes. Keeps GLSL
 * compilation off both the main and render threads.
 */
class MAHO_RENDER_API FShaderCompilerServer : public FThreadedServer
{
public:
	FShaderCompilerServer();
	~FShaderCompilerServer() override;

	/** Start the compile thread (idempotent). Returns false when glslang is missing. */
	bool Initialize();

	/** Submit an async compile; OnDone receives the result on the calling thread. */
	void CompileAsync(
		const FShaderCompileDesc& Desc,
		std::function<void(const FShaderCompileResult&)> OnDone);

	/** Flush: block until all submitted compiles complete. */
	void FlushCompiles();

	/** Synchronous compile (runs on the calling thread, not the server). */
	static FShaderCompileResult CompileStage(
		const FShaderCompileDesc& Desc);

private:
	void ProcessCompileJob(const FShaderCompileDesc& Desc,
		std::function<void(const FShaderCompileResult&)> OnDone);
};

} // namespace Maho
