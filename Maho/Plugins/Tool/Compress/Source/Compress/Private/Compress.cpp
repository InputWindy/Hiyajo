#include <Compress.h>

namespace Maho::Compress
{

bool FCompress::ExecuteStage(EToolStage Stage)
{
	// TODO: Init = init codecs; Shutdown = release.
	(void)Stage;
	return true;
}

} // namespace Maho::Compress

// ── Dynamic plugin entry (runtime load/unload via FAssemblyImporter) ──

namespace
{

class FCompressAdapter final : public Maho::IExtension<Maho::EToolStage>
{
public:
	[[nodiscard]] bool ExecuteStage(Maho::EToolStage Stage) override
	{
		return Maho::Compress::FCompress::Get().ExecuteStage(Stage);
	}
};

} // namespace

extern "C" MAHO_COMPRESS_API Maho::IExtension<Maho::EToolStage>* CreateExtension()
{
	return new FCompressAdapter();
}
