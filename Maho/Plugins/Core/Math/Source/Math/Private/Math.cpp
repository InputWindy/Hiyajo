#include <MathLibrary.h>

namespace Maho::Math
{

bool FMath::ExecuteStage(EToolStage Stage)
{
	// Pure function library: nothing to initialize or shut down.
	(void)Stage;
	return true;
}

} // namespace Maho::Math

// ── Dynamic plugin entry (runtime load/unload via FAssemblyImporter) ──

namespace
{

class FMathAdapter final : public Maho::IExtension<Maho::EToolStage>
{
public:
	[[nodiscard]] bool ExecuteStage(Maho::EToolStage Stage) override
	{
		return Maho::Math::FMath::Get().ExecuteStage(Stage);
	}
};

} // namespace

extern "C" MAHO_MATH_API Maho::IExtension<Maho::EToolStage>* CreateExtension()
{
	return new FMathAdapter();
}
