#include <Audio.h>

namespace Maho::Audio
{

bool FAudio::ExecuteStage(EToolStage Stage)
{
	// TODO: Init = open audio device; Shutdown = close.
	(void)Stage;
	return true;
}

} // namespace Maho::Audio

// ── Dynamic plugin entry (runtime load/unload via FAssemblyImporter) ──

namespace
{

class FAudioAdapter final : public Maho::IExtension<Maho::EToolStage>
{
public:
	[[nodiscard]] bool ExecuteStage(Maho::EToolStage Stage) override
	{
		return Maho::Audio::FAudio::Get().ExecuteStage(Stage);
	}
};

} // namespace

extern "C" MAHO_AUDIO_API Maho::IExtension<Maho::EToolStage>* CreateExtension()
{
	return new FAudioAdapter();
}
