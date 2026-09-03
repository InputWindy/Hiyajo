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

/**
 * Shader CONTENT fingerprint (FNV-1a 64-bit over the SPIR-V words). The PSO cache
 * keys a graphics pipeline on its VS/FS content hash, because shader modules are
 * transient (rebuilt from identical bytes across passes) -- content, not pointers,
 * is the stable identity.
 */
inline std::uint64_t HashShaderWords(const std::uint32_t* Words, std::size_t Count)
{
	std::uint64_t H = 1469598103934665603ULL;
	for (std::size_t I = 0; I < Count; ++I)
	{
		H ^= Words[I];
		H *= 1099511628211ULL;
	}
	return H;
}

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
