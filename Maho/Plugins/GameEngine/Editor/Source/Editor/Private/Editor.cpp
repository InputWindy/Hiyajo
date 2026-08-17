#include <Editor.h>

namespace Maho::Editor
{

bool FEditorSystem::ExecuteStage(EEngineStage Stage)
{
	// TODO: Init = build editor UI; Tick = draw; Shutdown = tear down.
	(void)Stage;
	return true;
}

} // namespace Maho::Editor

// ── Dynamic plugin entry (runtime load/unload via FAssemblyImporter) ──

namespace
{

class FEditorSystemAdapter final : public Maho::IExtension<Maho::EEngineStage>
{
public:
	[[nodiscard]] bool ExecuteStage(Maho::EEngineStage Stage) override
	{
		return Maho::Editor::FEditorSystem::Get().ExecuteStage(Stage);
	}
};

} // namespace

extern "C" MAHO_EDITOR_API Maho::IExtension<Maho::EEngineStage>* CreateExtension()
{
	return new FEditorSystemAdapter();
}
