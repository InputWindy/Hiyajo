#pragma once

#include "RenderApi.h"
#include <Core/Async/ThreadedServer.h>
#include <Engine.h>

namespace Maho
{

namespace Render
{

	/** Render/RHI/RDG/Shader/UI extension. Engine extension (driven by EEngineStage). */
	class MAHO_RENDER_API FRenderSystem
		: public TExtension<EEngineStage, FRenderSystem>
		, public FThreadedServer
	{
	public:
		[[nodiscard]] bool ExecuteStage(EEngineStage Stage) override;

	protected:
		[[nodiscard]] const char* GetThreadName() const override;

	private:
		friend TSingleton<FRenderSystem>;
		FRenderSystem() = default;
	};

} // namespace Render

} // namespace Maho
