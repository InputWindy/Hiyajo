#pragma once

#include "NetworkApi.h"
#include <Core/Core.h>

namespace Maho
{

namespace Network
{

	/** Network communication extension (client/server). Engine extension (driven by EEngineStage). */
	class MAHO_NETWORK_API FNetworkSystem : public TExtension<EEngineStage, FNetworkSystem>
	{
	public:
		[[nodiscard]] bool ExecuteStage(EEngineStage Stage) override;

	private:
		friend TSingleton<FNetworkSystem>;
		FNetworkSystem() = default;
	};

} // namespace Network

} // namespace Maho
