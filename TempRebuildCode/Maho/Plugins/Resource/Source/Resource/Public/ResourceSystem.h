#pragma once

#include "ResourceApi.h"
#include <Core/Async/ThreadedServer.h>
#include <Engine.h>

namespace Maho
{

namespace Resource
{

	/** Async resource system + package IO extension. Engine extension (driven by EEngineStage). */
	class MAHO_RESOURCE_API FResourceSystem final
		: public TExtension<EEngineStage, FResourceSystem>
		, public FThreadedServer
	{
	public:
		[[nodiscard]] bool ExecuteStage(EEngineStage Stage) override;

	protected:
		[[nodiscard]] const char* GetThreadName() const override;

	private:
		friend TSingleton<FResourceSystem>;
		FResourceSystem() = default;
	};

} // namespace Resource

} // namespace Maho
