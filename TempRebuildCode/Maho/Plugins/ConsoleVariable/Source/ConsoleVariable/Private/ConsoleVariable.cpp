#include <ConsoleVariable.h>

namespace Maho::ConsoleVariable
{

bool FConsoleVariable::ExecuteStage(EToolStage Stage)
{
	// TODO: Init = register builtin cvars; Shutdown = unregister.
	(void)Stage;
	return true;
}

} // namespace Maho::ConsoleVariable

// ── Dynamic plugin entry (runtime load/unload via FPluginManager) ──

namespace
{

class FConsoleVariableAdapter final : public Maho::IExtension<Maho::EToolStage>
{
public:
	[[nodiscard]] bool ExecuteStage(Maho::EToolStage Stage) override
	{
		return Maho::ConsoleVariable::FConsoleVariable::Get().ExecuteStage(Stage);
	}
};

} // namespace

extern "C" MAHO_CONSOLEVARIABLE_API Maho::IExtension<Maho::EToolStage>* CreateExtension()
{
	return new FConsoleVariableAdapter();
}
