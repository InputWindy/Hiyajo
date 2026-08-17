#include <Network.h>

namespace Maho::Network
{

bool FNetworkSystem::ExecuteStage(EEngineStage Stage)
{
	// TODO: Init = start network stack; Tick = pump; Shutdown = stop.
	(void)Stage;
	return true;
}

} // namespace Maho::Network

// ── Dynamic plugin entry (runtime load/unload via FAssemblyImporter) ──

namespace
{

class FNetworkSystemAdapter final : public Maho::IExtension<Maho::EEngineStage>
{
public:
	[[nodiscard]] bool ExecuteStage(Maho::EEngineStage Stage) override
	{
		return Maho::Network::FNetworkSystem::Get().ExecuteStage(Stage);
	}
};

} // namespace

extern "C" MAHO_NETWORK_API Maho::IExtension<Maho::EEngineStage>* CreateExtension()
{
	return new FNetworkSystemAdapter();
}
